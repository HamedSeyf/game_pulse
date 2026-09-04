
#include <algorithm>
#include <cassert>
#include <charconv>
#include <iostream>
#include <vector>
#include <memory>
#include <string_view>
#include <system_error>

import game_pulse.analytics;
import game_pulse.domain;
import game_pulse.pipeline;
import game_pulse.queue;
import game_pulse.simulation;

template <typename T>
concept ChronoDuration =
    requires
{
    typename T::rep;
    typename T::period;
};

int main(int argc, char** argv)
{
    std::shared_ptr<Configuration> cfg = std::make_shared<Configuration>();

    // Parsing the passed arguments to derive Configuration
    const auto parse_value = []<typename T>(std::string_view value, T& destination) noexcept
    {
        if constexpr (ChronoDuration<T>)
        {
            typename T::rep count{};

            const auto [end, error] = std::from_chars(
                value.data(),
                value.data() + value.size(),
                count);

            if (error != std::errc{} || end != value.data() + value.size())
            {
                return false;
            }

            destination = T{ count };

            return true;
        }
        else
        {
            const auto [end, error] = std::from_chars(
                value.data(),
                value.data() + value.size(),
                destination);

            return error == std::errc{} && end == value.data() + value.size();
        }
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
            if (!parse_value(value, cfg->queue_capacity))
                return 2;
        }
        else if (read_value("--batch-size"))
        {
            if (!parse_value(value, cfg->batch_size))
                return 2;
        }
        else if (read_value("--player-count"))
        {
            if (!parse_value(value, cfg->player_count))
                return 2;
        }
        else if (read_value("--snapshot-interval"))
        {
            if (!parse_value(value, cfg->snapshot_interval))
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

    try
    {
        std::shared_ptr<TickClock> tickClock = std::make_shared<TickClock>(cfg->tick_duration);
        std::shared_ptr<Analytics> analytics = std::make_shared<Analytics>();
        std::shared_ptr<Queue> queue = std::make_shared<Queue>(cfg->queue_capacity);
        std::shared_ptr<Pipeline> pipeline = std::make_shared<Pipeline>(tickClock, queue, cfg->batch_size);

        if (const auto result = pipeline->RegisterProcessor(analytics); !result)
        {
            assert(false && "Failed to register the analytics.");
            return 0;
        }

        /* Simulators for player related events */
        std::vector<T_ID> playerIDs(cfg->player_count);
        std::generate(
            playerIDs.begin(),
            playerIDs.end(),
            []()
            {
                return GlobalID::NextID();
            });      

        const SimulationTypes::TEventGenerationWeights eventGenerationWeights{
            .spawnWeight = 0.10,
            .moveWeight = 0.40,
            .shotWeight = 0.30,
            .noEventWeight = 0.20,
        };
        constexpr std::uint64_t masterSimulationSeed = 0x5EED'2026ULL;

        std::vector<std::shared_ptr<Simulation>> simulators;
        simulators.reserve(cfg->player_count);

        for (const auto currentPlayerId : playerIDs)
        {
            auto otherPlayerIDs = playerIDs;

            std::erase(otherPlayerIDs, currentPlayerId);

            std::shared_ptr<Simulation> simulator = std::make_shared<Simulation>
                (
                    tickClock,
                    queue,
                    currentPlayerId,
                    otherPlayerIDs,
                    eventGenerationWeights,
                    masterSimulationSeed + currentPlayerId
                );

            simulators.push_back(simulator);
        }

        if (const auto result = queue->SwitchToState(TStateMachineState::InProgress); !result)
        {
            assert(false && "Failed to start the queue.");
            return 0;
        }

        for (auto& currentSimulator : simulators)
        {
            if (const auto result = currentSimulator->SwitchToState(SimulationTypes::TSimulationStateMachineState::InProgress); !result)
            {
                assert(false && "Failed to start simulator(s).");
                return 0;
            }
        }

        if (const auto result = pipeline->SwitchToState(TStateMachineState::InProgress); !result)
        {
            assert(false && "Failed to start the pipeline.");
            return 0;
        }

        pipeline->JoinAndWait();
    }
    catch (const std::exception& e)
    {
        assert(false && "Failed to instantiate and/or start.");
        // For now we keep it to a simple cerr as proper logging is not within the scope of this code demonstration
        std::cerr << e.what() << '\n';
        return 0;
    }
    catch (...)
    {
        assert(false && "Failed to instantiate and/or start.");
        // For now we keep it to a simple cerr as proper logging is not within the scope of this code demonstration
        std::cerr << "Unknown non-std::exception thrown inside main().\n";
        return 0;
    }

    return 1;
}
