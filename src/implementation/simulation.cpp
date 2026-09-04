module game_pulse.simulation;

import game_pulse.pipeline;
import game_pulse.queue;

import <cassert>;
import <iostream>;
import <optional>;


Simulation::Simulation(
    std::shared_ptr<TickClock> tickClock,
    std::shared_ptr<Queue> queue,
    T_ID playerID,
    const std::span<T_ID> otherPlayerIDs,
    SimulationTypes::TEventGenerationWeights eventGenerationWeights,
    std::uint64_t randomSeed
)
    :
    _playerID(playerID),
    _otherPlayerIDs(otherPlayerIDs.begin(), otherPlayerIDs.end()),
    _tickClock(std::move(tickClock)),
    _queue(std::move(queue)),
    _eventGenerationCutoffs(BuildEventGenerationCutoffs(eventGenerationWeights)),
    _randomEngine(randomSeed),
    _targetDistribution{ 0, otherPlayerIDs.size() - 1 },
    _damageDistribution{ 1, PlayerMaxHealth }
{
    if (!_tickClock || !_queue || otherPlayerIDs.empty())
    {
        throw std::invalid_argument{ "Invalid tickClock, queue or otherPlayerIDs passed to Simulation's ctor." };
    }
}

Simulation::TEventGenerationCutoffs Simulation::BuildEventGenerationCutoffs(const SimulationTypes::TEventGenerationWeights& weights)
{
    const auto isValidWeight = [](const double weight) noexcept
        {
            return std::isfinite(weight) && weight >= 0.0;
        };

    if (!isValidWeight(weights.spawnWeight) ||
        !isValidWeight(weights.moveWeight) ||
        !isValidWeight(weights.shotWeight) ||
        !isValidWeight(weights.noEventWeight))
    {
        throw std::invalid_argument{
            "Event-generation weights must be finite and nonnegative."
        };
    }

    const double totalWeight =
        weights.spawnWeight +
        weights.moveWeight +
        weights.shotWeight +
        weights.noEventWeight;

    if (!std::isfinite(totalWeight) || totalWeight <= 0.0)
    {
        throw std::invalid_argument{
            "Event-generation weights must have a positive finite total."
        };
    }

    const double inverseTotal = 1.0 / totalWeight;

    const double spawnEnd = weights.spawnWeight * inverseTotal;

    const double moveEnd = spawnEnd + weights.moveWeight * inverseTotal;

    const double shotEnd = (weights.noEventWeight == 0.0 ? 1.0 : moveEnd + weights.shotWeight * inverseTotal);

    return TEventGenerationCutoffs{
        .spawnEnd = spawnEnd,
        .moveEnd = moveEnd,
        .shotEnd = shotEnd,
    };
}

std::optional<EventTypes::Event> Simulation::CreateRandomEvent(const TickClock::Tick tick)
{
    const double sample = _unitDistribution(_randomEngine);

    const auto makeRandomPosition = [this]() -> TPlayerPositionType
        {
            return {
                _unitDistribution(_randomEngine),
                _unitDistribution(_randomEngine)
            };
        };

    if (sample < _eventGenerationCutoffs.spawnEnd)
    {
        return EventTypes::Event
        {
            .id = GlobalID::NextID(),
            .tick = tick,
            .type = EventTypes::EventType::Spawn,
            .spawn = EventTypes::SpawnEvent
            {
                .playerId = _playerID,
                .position = makeRandomPosition()
            }
        };
    }
    else if (sample < _eventGenerationCutoffs.moveEnd)
    {
        return EventTypes::Event
        {
            .id = GlobalID::NextID(),
            .tick = tick,
            .type = EventTypes::EventType::Move,
            .move = EventTypes::MoveEvent
            {
                .playerId = _playerID,
                .position = makeRandomPosition()
            }
        };
    }
    else if (sample < _eventGenerationCutoffs.shotEnd)
    {
        return EventTypes::Event
        {
            .id = GlobalID::NextID(),
            .tick = tick,
            .type = EventTypes::EventType::Shot,
            .shot = EventTypes::ShotEvent
            {
                .shooterId = _playerID,
                .targetId = _otherPlayerIDs[_targetDistribution(_randomEngine)],
                .damage = _damageDistribution(_randomEngine)
            }
        };
    }

    return std::nullopt;
}

void Simulation::OnStateTransitionLocked(const SimulationTypes::TSimulationStateMachineState newState) noexcept
{
    TStateMachine::OnStateTransitionLocked(newState);

    try
    {
        if (newState == SimulationTypes::TSimulationStateMachineState::InProgress)
        {
            if (const auto result = _queue->RegisterSimulator(shared_from_this()); result)
            {
                _queueRegistrationHandle = result.value();
            }
            else
            {
                throw std::invalid_argument{ "Simulation failed to register with queue." };
            }

            _workerThread = std::jthread([this](std::stop_token stopToken)
                {
                    WorkerMain(stopToken);
                }
            );
        }
        else if (newState == SimulationTypes::TSimulationStateMachineState::Stopped)
        {
            _workerThread.request_stop();
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    catch (...)
    {
        std::cerr << "Unknown non-std::exception thrown inside Simulation::OnStateTransitionLocked.\n";
    }
}

void Simulation::WorkerMain(std::stop_token stopToken)
{
    std::mutex tickWaitMutex;
    std::condition_variable_any tickWaitCV;

    T_Tick tick = _tickClock->GetCurrentTick();

    while (!stopToken.stop_requested())
    {
        try
        {
            if (const auto RandomEvent = CreateRandomEvent(tick); RandomEvent)
            {
                if (const auto result = _queue->WaitAndPush(_playerID, RandomEvent.value(), tick, stopToken); !result)
                {
                    if (stopToken.stop_requested() && result.error() == QueueTypes::Error::operation_cancelled)
                    {
                        break;
                    }

                    std::cerr << "Simulation failed to push the created event to queue. Exitting this simulator.\n";
                    assert(false);
                    break;
                }
            }
            else if (!_queue->UpdateSimulatorWatermark(_queueRegistrationHandle.value(), tick))
            {
                std::cerr << "Simulator failed to update queue with its latest watermark.\n";
                assert(false);
                break;
            }

            {
                std::unique_lock lock{ tickWaitMutex };

                (void)tickWaitCV.wait_until(
                    lock,
                    stopToken,
                    _tickClock->GetStartOfTick(tick + 1),
                    [this, tick]
                    {
                        return _tickClock->GetCurrentTick() > tick;
                    });
            }

            tick = _tickClock->GetCurrentTick();

        }
        catch (const std::exception& e)
        {
            std::cerr << e.what() << '\n';
            break;
        }
        catch (...)
        {
            std::cerr << "Unknown non-std::exception thrown inside Simulation::WorkerMain.\n";
            break;
        }
    }

    SwitchToState(SimulationTypes::TSimulationStateMachineState::Stopped);
}
