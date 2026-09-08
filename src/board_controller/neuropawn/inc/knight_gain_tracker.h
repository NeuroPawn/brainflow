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

    std::vector<int> current_gains;
    std::vector<int> old_gains;
    std::vector<int> available_gain_values;

    bool is_allowed_gain (int gain) const
    {
        return std::find (available_gain_values.begin (), available_gain_values.end (), gain) !=
            available_gain_values.end ();
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
        return (int)KnightCommandTypes::VALID_COMMAND;
    }

public:
    KnightGainTracker ()
        : current_gains (num_channels, default_gain), old_gains (num_channels, default_gain)
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

    virtual void set_all_gains (int gain)
    {
        if (!is_allowed_gain (gain))
        {
            return;
        }
        std::copy (current_gains.begin (), current_gains.end (), old_gains.begin ());
        std::fill (current_gains.begin (), current_gains.end (), gain);
    }

    virtual bool is_valid_gain (int gain) const
    {
        return is_allowed_gain (gain);
    }

    virtual void revert_config ()
    {
        std::copy (old_gains.begin (), old_gains.end (), current_gains.begin ());
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
