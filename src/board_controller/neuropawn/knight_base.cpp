#include <chrono>
#include <math.h>
#include <string.h>
#include <string>
#include <thread>
#include <vector>

#include "custom_cast.h"
#include "json.hpp"
#include "knight_base.h"
#include "serial.h"
#include "timestamp.h"

using json = nlohmann::json;

constexpr int KnightBase::start_byte;
constexpr int KnightBase::end_byte;

// Brief pauses so consecutive serial commands are accepted reliably.
namespace
{
constexpr int knight_stream_settle_ms = 250;
constexpr int knight_command_gap_ms = 1250;
}

KnightBase::KnightBase (int board_id, struct BrainFlowInputParams params) : Board (board_id, params)
{
    serial = NULL;
    is_streaming = false;
    keep_alive = false;
    initialized = false;
}

KnightBase::~KnightBase ()
{
    skip_logs = true;
    release_session ();
}

int KnightBase::prepare_session ()
{
    if (initialized)
    {
        safe_logger (spdlog::level::info, "Session already prepared");
        return (int)BrainFlowExitCodes::STATUS_OK;
    }
    if (params.serial_port.empty ())
    {
        safe_logger (spdlog::level::err, "serial port is empty");
        return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
    }

    std::vector<KnightChannelSetup> channels;
    int parse_res = parse_other_info (channels);
    if (parse_res != (int)BrainFlowExitCodes::STATUS_OK)
    {
        return parse_res;
    }

    serial = Serial::create (params.serial_port.c_str (), this);
    int port_open = open_port ();
    if (port_open != (int)BrainFlowExitCodes::STATUS_OK)
    {
        delete serial;
        serial = NULL;
        return port_open;
    }

    int set_settings = set_port_settings ();
    if (set_settings != (int)BrainFlowExitCodes::STATUS_OK)
    {
        delete serial;
        serial = NULL;
        return set_settings;
    }

    if (!channels.empty ())
    {
        int apply_res = apply_other_info (channels);
        if (apply_res != (int)BrainFlowExitCodes::STATUS_OK)
        {
            delete serial;
            serial = NULL;
            return apply_res;
        }
    }

    initialized = true;
    return (int)BrainFlowExitCodes::STATUS_OK;
}

int KnightBase::start_stream (int buffer_size, const char *streamer_params)
{
    if (is_streaming)
    {
        safe_logger (spdlog::level::err, "Streaming thread already running");
        return (int)BrainFlowExitCodes::STREAM_ALREADY_RUN_ERROR;
    }
    int res = prepare_for_acquisition (buffer_size, streamer_params);
    if (res != (int)BrainFlowExitCodes::STATUS_OK)
    {
        return res;
    }

    serial->flush_buffer ();

    keep_alive = true;
    streaming_thread = std::thread ([this] { this->read_thread (); });
    is_streaming = true;
    // Allow the board to settle before the first config command.
    std::this_thread::sleep_for (std::chrono::milliseconds (knight_stream_settle_ms));
    return (int)BrainFlowExitCodes::STATUS_OK;
}

int KnightBase::stop_stream ()
{
    if (is_streaming)
    {
        keep_alive = false;
        is_streaming = false;
        if (streaming_thread.joinable ())
        {
            streaming_thread.join ();
        }
        return (int)BrainFlowExitCodes::STATUS_OK;
    }
    else
    {
        return (int)BrainFlowExitCodes::STREAM_THREAD_IS_NOT_RUNNING;
    }
}

int KnightBase::release_session ()
{
    if (initialized)
    {
        if (is_streaming)
        {
            stop_stream ();
        }
        free_packages ();
        initialized = false;
    }
    if (serial)
    {
        serial->close_serial_port ();
        delete serial;
        serial = NULL;
    }
    return (int)BrainFlowExitCodes::STATUS_OK;
}

int KnightBase::open_port ()
{
    if (serial->is_port_open ())
    {
        safe_logger (spdlog::level::err, "port {} already open", serial->get_port_name ());
        return (int)BrainFlowExitCodes::PORT_ALREADY_OPEN_ERROR;
    }

    safe_logger (spdlog::level::info, "openning port {}", serial->get_port_name ());
    int res = serial->open_serial_port ();
    if (res < 0)
    {
        return (int)BrainFlowExitCodes::UNABLE_TO_OPEN_PORT_ERROR;
    }
    safe_logger (spdlog::level::trace, "port {} is open", serial->get_port_name ());
    return (int)BrainFlowExitCodes::STATUS_OK;
}

int KnightBase::set_port_settings ()
{
    int res = serial->set_serial_port_settings (1000, false);
    if (res < 0)
    {
        safe_logger (spdlog::level::err, "Unable to set port settings, res is {}", res);
        return (int)BrainFlowExitCodes::SET_PORT_ERROR;
    }
    res = serial->set_custom_baudrate (115200);
    if (res < 0)
    {
        safe_logger (spdlog::level::err, "Unable to set custom baud rate, res is {}", res);
        return (int)BrainFlowExitCodes::SET_PORT_ERROR;
    }
    safe_logger (spdlog::level::trace, "set port settings");
    return (int)BrainFlowExitCodes::STATUS_OK;
}

static std::string trim_other_info (const std::string &value)
{
    size_t first = value.find_first_not_of (" \t\r\n");
    if (first == std::string::npos)
    {
        return "";
    }
    size_t last = value.find_last_not_of (" \t\r\n");
    return value.substr (first, last - first + 1);
}

int KnightBase::parse_other_info (std::vector<KnightChannelSetup> &channels)
{
    channels.clear ();

    std::string trimmed = trim_other_info (params.other_info);
    if (trimmed.empty ())
    {
        return (int)BrainFlowExitCodes::STATUS_OK;
    }

    try
    {
        json info = json::parse (trimmed);
        if (!info.is_object ())
        {
            safe_logger (spdlog::level::err,
                "Invalid Knight other_info: expected a JSON object of channels");
            return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
        }

        for (auto it = info.begin (); it != info.end (); ++it)
        {
            const std::string key = it.key ();
            if (key.size () != 1 || key[0] < '1' || key[0] > '8')
            {
                safe_logger (spdlog::level::err,
                    "Invalid Knight other_info: unknown channel key {}", key.c_str ());
                return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
            }
            if (!it.value ().is_object ())
            {
                safe_logger (spdlog::level::err,
                    "Invalid Knight other_info: channel {} must be an object", key.c_str ());
                return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
            }

            KnightChannelSetup setup;
            setup.channel = key[0] - '0';
            setup.gain = 12;
            setup.rld = false;

            const json &fields = it.value ();
            for (auto field = fields.begin (); field != fields.end (); ++field)
            {
                const std::string name = field.key ();
                if (name == "gain")
                {
                    if (!field.value ().is_number_integer ())
                    {
                        safe_logger (spdlog::level::err,
                            "Invalid Knight other_info: channel {} gain must be an integer",
                            setup.channel);
                        return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
                    }
                    int parsed_gain = field.value ().get<int> ();
                    if (!gain_tracker.is_valid_gain (parsed_gain))
                    {
                        safe_logger (spdlog::level::err,
                            "Invalid Knight other_info: channel {} gain {} is not allowed",
                            setup.channel, parsed_gain);
                        return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
                    }
                    setup.gain = parsed_gain;
                }
                else if (name == "rld")
                {
                    if (!field.value ().is_boolean ())
                    {
                        safe_logger (spdlog::level::err,
                            "Invalid Knight other_info: channel {} rld must be a boolean",
                            setup.channel);
                        return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
                    }
                    setup.rld = field.value ().get<bool> ();
                }
                else
                {
                    safe_logger (spdlog::level::err,
                        "Invalid Knight other_info: unknown key {} on channel {}", name.c_str (),
                        setup.channel);
                    return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
                }
            }

            channels.push_back (setup);
        }

        return (int)BrainFlowExitCodes::STATUS_OK;
    }
    catch (const json::exception &e)
    {
        safe_logger (spdlog::level::err, "Invalid Knight other_info: {}", e.what ());
        return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
    }
}

int KnightBase::apply_other_info (const std::vector<KnightChannelSetup> &channels)
{
    for (int channel = 1; channel <= 8; channel++)
    {
        const KnightChannelSetup *setup = NULL;
        for (size_t i = 0; i < channels.size (); i++)
        {
            if (channels[i].channel == channel)
            {
                setup = &channels[i];
                break;
            }
        }
        if (setup == NULL)
        {
            continue;
        }

        std::string command =
            std::string ("chon_") + std::to_string (channel) + "_" + std::to_string (setup->gain);
        std::string response;
        int res = send_to_board (command.c_str (), response);
        if (res != (int)BrainFlowExitCodes::STATUS_OK)
        {
            safe_logger (
                spdlog::level::err, "failed to set channel gain with {}", command.c_str ());
            return res;
        }

        if (setup->rld)
        {
            command = std::string ("rldadd_") + std::to_string (channel);
            res = send_to_board (command.c_str (), response);
            if (res != (int)BrainFlowExitCodes::STATUS_OK)
            {
                safe_logger (
                    spdlog::level::err, "failed to set channel rld with {}", command.c_str ());
                return res;
            }
        }
    }

    for (size_t i = 0; i < channels.size (); i++)
    {
        gain_tracker.set_gain_for_channel (channels[i].channel - 1, channels[i].gain);
    }
    safe_logger (spdlog::level::info, "initialized channel gains to {}",
        gain_tracker.get_gains_string ().c_str ());
    return (int)BrainFlowExitCodes::STATUS_OK;
}

int KnightBase::config_board (std::string config, std::string &response)
{
    if (!initialized)
    {
        return (int)BrainFlowExitCodes::BOARD_NOT_READY_ERROR;
    }

    int apply_res = gain_tracker.apply_config (config);
    if (apply_res == (int)KnightCommandTypes::INVALID_COMMAND)
    {
        safe_logger (spdlog::level::warn, "invalid command: {}", config.c_str ());
        return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
    }

    int res = (int)BrainFlowExitCodes::STATUS_OK;
    if (is_streaming)
    {
        safe_logger (spdlog::level::warn,
            "You are changing board params during streaming, it may lead to sync mismatch between "
            "data acquisition thread and device");
        res = send_to_board (config.c_str ());
    }
    else
    {
        // read response if streaming is not running
        res = send_to_board (config.c_str (), response);
    }

    if (res != (int)BrainFlowExitCodes::STATUS_OK)
    {
        gain_tracker.revert_config ();
    }

    return res;
}

int KnightBase::send_to_board (const char *msg)
{
    int length = (int)strlen (msg);
    safe_logger (spdlog::level::debug, "sending {} to the board", msg);
    int res = serial->send_to_serial_port ((const void *)msg, length);
    if (res != length)
    {
        return (int)BrainFlowExitCodes::BOARD_WRITE_ERROR;
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (knight_command_gap_ms));

    return (int)BrainFlowExitCodes::STATUS_OK;
}

int KnightBase::send_to_board (const char *msg, std::string &response)
{
    int length = (int)strlen (msg);
    safe_logger (spdlog::level::debug, "sending {} to the board", msg);
    int res = serial->send_to_serial_port ((const void *)msg, length);
    if (res != length)
    {
        response = "";
        return (int)BrainFlowExitCodes::BOARD_WRITE_ERROR;
    }
    response = read_serial_response ();
    std::this_thread::sleep_for (std::chrono::milliseconds (knight_command_gap_ms));

    return (int)BrainFlowExitCodes::STATUS_OK;
}

std::string KnightBase::read_serial_response ()
{
    constexpr int max_tmp_size = 4096;
    unsigned char tmp_array[max_tmp_size];
    unsigned char tmp;
    int tmp_id = 0;
    while (serial->read_from_serial_port (&tmp, 1) == 1)
    {
        if (tmp_id < max_tmp_size)
        {
            tmp_array[tmp_id] = tmp;
            tmp_id++;
        }
        else
        {
            serial->flush_buffer ();
            break;
        }
    }
    tmp_id = (tmp_id == max_tmp_size) ? tmp_id - 1 : tmp_id;
    tmp_array[tmp_id] = '\0';

    return std::string ((const char *)tmp_array);
}
