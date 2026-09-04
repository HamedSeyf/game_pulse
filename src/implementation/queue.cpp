module game_pulse.queue;

import game_pulse.simulation;

import <cassert>;
import <mutex>;
import <stdexcept>;


Queue::Queue(const std::size_t queue_capacity)
    : _events_queue { queue_capacity }
{
    if (queue_capacity == 0)
    {
        throw std::invalid_argument{ "Queue capacity must be greater than zero." };
    }
}

std::size_t Queue::GetSize() const
{
    std::lock_guard<std::mutex> lock(_state_mutex);
    return _events_queue.size();
}

std::expected<Queue::TSimulatorHandle, QueueTypes::Error> Queue::RegisterSimulator(std::shared_ptr<QueueTypes::SimulationInterface> simulator)
{
    if (!simulator)
    {
        return std::unexpected{ QueueTypes::Error::bad_arguments };
    }

    SimulatorEntry newSimulatorEntry{ std::move(simulator), std::make_shared<std::optional<T_Tick>>(std::nullopt) };

    {
        // TODO: since RegisterSimulator is not called occasionally and supposed to mostly be called right at the beginning of the pipeline, we use _state_mutex to assure two overlapping RegisterSimulator calls are not racing as long as one is dealing with the subscription. Thoughts?
        std::unique_lock lock{ _state_mutex };

        const auto foundSimulator = _subscriptionRegistry.forEachSubscribedObject(Queue::SubscriptionRegistryKey, [&simulator](const auto& currentSimulatorEntry)
            {
                return (currentSimulatorEntry.simulator == simulator);
            });

        if (foundSimulator)
        {
            return std::unexpected{ QueueTypes::Error::simulator_already_registered };
        }

        return { _subscriptionRegistry.subscribe(newSimulatorEntry, Queue::SubscriptionRegistryKey) };
    }
}

std::expected<void, QueueTypes::Error> Queue::UnRegisterSimulator(const TSimulatorHandle& handle)
{
    const bool success = _subscriptionRegistry.unsubscribe(handle);
    return success ? std::expected<void, QueueTypes::Error>{} : std::unexpected{ QueueTypes::Error::simulator_not_registered };
}

std::expected<void, QueueTypes::Error> Queue::WaitAndPush(TSimulatorHandle simulatorHandle, EventTypes::Event event, T_Tick completedThroughTick, std::stop_token stopToken)
{
    if (GetState() != TStateMachineState::InProgress)
    {
        return std::unexpected{ QueueTypes::Error::queue_not_started_or_shut_down };
    }

    {
        std::unique_lock<std::mutex> lock(_state_mutex);

        _queue_push_cv.wait(
            lock,
            stopToken,
            [this]
            {
                return GetState() != TStateMachineState::InProgress ||
                    !_events_queue.full();
            }
        );

        // Individual simulator cancellation.
        if (stopToken.stop_requested())
        {
            return std::unexpected{ QueueTypes::Error::operation_cancelled };
        }

        if (GetState() != TStateMachineState::InProgress)
        {
            return std::unexpected{ QueueTypes::Error::queue_not_started_or_shut_down };
        }

        if (!_events_queue.try_emplace(std::move(event)))
        {
            assert(false && "Broken internal logic as _events_queue should not be full and std::move should work on event objects.");
            return std::unexpected{ QueueTypes::Error::internal_error };
        }

        if (!UpdateSimulatorWatermarkUnlocked(std::move(simulatorHandle), std::move(completedThroughTick)))
        {
            return std::unexpected{ QueueTypes::Error::regressing_watermark_passed };
        }
    }

    _queue_pop_cv.notify_one();

    return {};
}

std::expected<std::span<EventTypes::Event>, QueueTypes::Error> Queue::WaitAndPop(std::span<EventTypes::Event> destination, T_Tick throughTick, std::stop_token stopToken)
{
    if (GetState() != TStateMachineState::InProgress)
    {
        return std::unexpected{ QueueTypes::Error::queue_not_started_or_shut_down };
    }
    
    if (destination.size() == 0)
    {
        return std::unexpected{ QueueTypes::Error::bad_arguments };
    }

    std::unique_lock lock{ _state_mutex };

    _queue_pop_cv.wait(
        lock,
        stopToken,
        [this]
        {
            return !_events_queue.empty() ||
                GetState() == TStateMachineState::Stopped;
        }
    );

    // Individual simulator cancellation.
    if (stopToken.stop_requested())
    {
        return std::unexpected{ QueueTypes::Error::operation_cancelled };
    }

    const TStateMachineState cached_state = GetState();

    if (cached_state == TStateMachineState::Stopped)
    {
        return std::unexpected{ QueueTypes::Error::queue_not_started_or_shut_down };
    }

    const bool was_full = _events_queue.full();

    (void)_subscriptionRegistry.forEachSubscribedObject(Queue::SubscriptionRegistryKey, [&throughTick](const auto& currentSimulatorEntry)
        {
            if (currentSimulatorEntry.completedThroughTick)
            {
                throughTick = std::min(throughTick, currentSimulatorEntry.completedThroughTick->value_or(std::numeric_limits<T_Tick>::max()));
            }
            return false;
        });

    const auto retval_span = _events_queue.pop_into(destination, [throughTick](const auto& event)
        {
            return event.tick <= throughTick;
        });
    const bool shouldStop = cached_state == TStateMachineState::Stopping_Gracefully && _events_queue.empty();

    if (shouldStop)
    {
        SwitchToStateLocked(lock, TStateMachineState::Stopped);
    }

    lock.unlock();

    if (shouldStop)
    {
        OnStateTransitionUnlocked(TStateMachineState::Stopped);
    }

    // Check whether or not we should notify all waiters
    if (cached_state == TStateMachineState::InProgress && was_full)
    {
        _queue_push_cv.notify_all();
    }

    return retval_span;
}

std::expected<void, QueueTypes::Error> Queue::UpdateSimulatorWatermark(T_ID simulatorId, T_Tick completedThroughTick)
{
    {
        std::lock_guard lock{ _state_mutex };

        if (const auto cached_state = GetState(); cached_state != TStateMachineState::InProgress)
        {
            return std::unexpected{ QueueTypes::Error::queue_not_started_or_shut_down };
        }

        if (!UpdateSimulatorWatermarkUnlocked(std::move(simulatorId), std::move(completedThroughTick)))
        {
            return std::unexpected{ QueueTypes::Error::regressing_watermark_passed };
        }
    }

    _queue_pop_cv.notify_one();

    return {};
}

void Queue::OnStateTransitionLocked(const TStateMachineState newState) noexcept
{
    TStateMachine::OnStateTransitionLocked(newState);

    if (newState == TStateMachineState::Stopped)
    {
        _events_queue.clear();
    }
}

void Queue::OnStateTransitionUnlocked(const TStateMachineState newState) noexcept
{
    if (newState == TStateMachineState::Stopping_Gracefully || newState == TStateMachineState::Stopped)
    {
        _queue_push_cv.notify_all();
        _queue_pop_cv.notify_all();
    }
}

bool Queue::UpdateSimulatorWatermarkUnlocked(TSimulatorHandle simulatorHandle, T_Tick completedThroughTick)
{
    if (auto foundSimulator = _subscriptionRegistry.getSubscribedObject(Queue::SubscriptionRegistryKey, simulatorHandle); foundSimulator)
    {
        foundSimulator->completedThroughTick->emplace(std::move(completedThroughTick));
        return true;
    }
    return false;
}
