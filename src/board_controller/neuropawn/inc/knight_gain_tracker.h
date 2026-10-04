#pragma once

#include <algorithm>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <vector>


enum class KnightCommandTypes : int
{
    NOT_CHANNEL_COMMAND = 0,
    VALID_COMMAND = 1,
    INVALID_COMMAND = 2
};


class KnightGainTracker
{
protected:
    enum
    {
        num_channels = 8,
        default_gain = 12
    };

    // ADS1198: 4Vref / (2^15 - 1) / gain / 79.57 * 1e6 -> uV
    static constexpr double eeg_scale_base = 4.0 / (32768.0 - 1.0) / 79.57 * 1000000.0;

    std::vector<int> current_gains;
    std::vector<int> old_gains;
    std::vector<double> current_scales;
    std::vector<int> available_gain_values;

    bool is_allowed_gain (int gain) const
    {
        return std::find (available_gain_values.begin (), available_gain_values.end (), gain) !=
            available_gain_values.end ();
    }

    void update_scale_for_channel (size_t index)
    {
        current_scales[index] = eeg_scale_base / (double)current_gains[index];
    }

    void update_all_scales ()
    {
        for (size_t i = 0; i < current_gains.size (); i++)
        {
            update_scale_for_channel (i);
        }
    }

    int apply_chon_command (const std::string &command)
    {
        // Firmware: chon_<ch>_<gain> with single-digit channel (1-8)
        // e.g. "chon_1_12"
        if (command.size () < 8 || command.compare (0, 5, "chon_") != 0)
        {
            return (int)KnightCommandTypes::NOT_CHANNEL_COMMAND;
        }

        char channel_char = command.at (5);
        if (channel_char < '1' || channel_char > '8')
        {
            return (int)KnightCommandTypes::INVALID_COMMAND;
        }
        if (command.at (6) != '_')
        {
            return (int)KnightCommandTypes::INVALID_COMMAND;
        }

        char *end = NULL;
        long gain = strtol (command.c_str () + 7, &end, 10);
        if (end == command.c_str () + 7 || *end != '\0')
        {
            return (int)KnightCommandTypes::INVALID_COMMAND;
        }
        if (!is_allowed_gain ((int)gain))
        {
            return (int)KnightCommandTypes::INVALID_COMMAND;
        }

        size_t index = (size_t)(channel_char - '1');
        old_gains[index] = current_gains[index];
        current_gains[index] = (int)gain;
        update_scale_for_channel (index);
        return (int)KnightCommandTypes::VALID_COMMAND;
    }

public:
    KnightGainTracker ()
        : current_gains (num_channels, default_gain)
        , old_gains (num_channels, default_gain)
        , current_scales (num_channels, eeg_scale_base / (double)default_gain)
    {
        available_gain_values = std::vector<int> {1, 2, 3, 4, 6, 8, 12};
    }

    virtual ~KnightGainTracker ()
    {
    }

    virtual int apply_config (std::string config)
    {
        if (config.compare (0, 5, "chon_") == 0)
        {
            return apply_chon_command (config);
        }
        // choff_*, rldadd_*, rldremove_*, and other board cmds do not change gain state
        return (int)KnightCommandTypes::NOT_CHANNEL_COMMAND;
    }

    virtual int get_gain_for_channel (int channel)
    {
        if (channel < 0 || channel >= (int)current_gains.size ())
        {
            return default_gain;
        }
        return current_gains[channel];
    }

    virtual double get_scale_for_channel (int channel)
    {
        if (channel < 0 || channel >= (int)current_scales.size ())
        {
            return eeg_scale_base / (double)default_gain;
        }
        return current_scales[channel];
    }

    virtual void set_gain_for_channel (int channel, int gain)
    {
        if (channel < 0 || channel >= (int)current_gains.size () || !is_allowed_gain (gain))
        {
            return;
        }
        current_gains[channel] = gain;
        old_gains[channel] = gain;
        update_scale_for_channel ((size_t)channel);
    }

    virtual bool is_valid_gain (int gain) const
    {
        return is_allowed_gain (gain);
    }

    virtual void revert_config ()
    {
        std::copy (old_gains.begin (), old_gains.end (), current_gains.begin ());
        update_all_scales ();
    }

    virtual std::string get_gains_string ()
    {
        std::stringstream gains;
        for (size_t i = 0; i < current_gains.size (); i++)
        {
            gains << current_gains[i];
            if (i < current_gains.size () - 1)
            {
                gains << ", ";
            }
        }
        return gains.str ();
    }
};
