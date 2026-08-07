
#include <charconv>
#include <vector>
#include <memory>
#include <string_view>
#include <system_error>

import game_pulse.domain;
import game_pulse.queue;

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
    std::shared_ptr<Configuration> cfg = std::make_shared<Configuration>();

    // Parsing the passed arguments to derive Configuration
    const auto parse_size = [](std::string_view value, std::size_t& destination) noexcept {
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), destination);
        return error == std::errc{} && end == value.data() + value.size();
    };

    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        std::string_view value;

        const auto read_value = [&](std::string_view option) noexcept {
            if (argument == option)
            {
                if (++index == argc)
                    return false;
                value = argv[index];
                return true;
            }

            if (argument.size() > option.size() && argument.starts_with(option)
                && argument[option.size()] == '=')
            {
                value = argument.substr(option.size() + 1);
                return true;
            }

            return false;
        };

        if (read_value("--queue-size"))
        {
            if (!parse_size(value, cfg->queue_capacity))
                return 2;
        }
        else if (read_value("--batch-size"))
        {
            if (!parse_size(value, cfg->batch_size))
                return 2;
        }
        else if (read_value("--producer-count"))
        {
            if (!parse_size(value, cfg->producer_count))
                return 2;
        }
        else if (read_value("--snapshot-interval"))
        {
            if (!parse_size(value, cfg->snapshot_interval))
                return 2;
        }
        else if (read_value("--shutdown-gracefully"))
        {
            if (value == "true" || value == "1")
                cfg->shutdown_gracefully = true;
            else if (value == "false" || value == "0")
                cfg->shutdown_gracefully = false;
            else
                return 2;
        }
        else
        {
            return 2;
        }
    }

    // TODO: Hamed should modify the logic here later on
    Queue queue { cfg->queue_capacity };

    std::vector<Event> destination_for_testing(cfg->batch_size);

    if (const auto returned_value = queue.WaitAndPopBatch(destination_for_testing); !returned_value.has_value())
    {
        // TODO: for hamed: Something went wrong so log it here ???
    }

    return 1;
}
