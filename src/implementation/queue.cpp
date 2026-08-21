module game_pulse.queue;

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

std::expected<void, QueueTypes::Error> Queue::WaitAndPush(EventTypes::Event event)
{
    {
        std::unique_lock<std::mutex> lock(_state_mutex);

        _queue_push_cv.wait(
            lock,
            [this]
            {
                const auto state = GetState();
                return !_events_queue.full() ||
                    state == TStateMachineState::Stopping_Gracefully ||
                    state == TStateMachineState::Stopped;
            }
        );

        if (const auto cached_state = GetState(); cached_state == TStateMachineState::Stopping_Gracefully || cached_state == TStateMachineState::Stopped)
        {
            return std::unexpected{ QueueTypes::Error::queue_not_started_or_shut_down };
        }

        if (!_events_queue.try_emplace(std::move(event)))
        {
            assert(false && "Broken internal logic as _events_queue should not be full and std::move should work on event objects.");
            return std::unexpected{ QueueTypes::Error::internal_error };
        }
    }

    _queue_pop_cv.notify_one();

    return {};
}

std::expected<std::span<EventTypes::Event>, QueueTypes::Error> Queue::WaitAndPopBatch(std::span<EventTypes::Event> destination)
{
    if (destination.size() == 0)
    {
        return std::unexpected{ QueueTypes::Error::bad_arguments };
    }

    std::unique_lock lock{ _state_mutex };

    _queue_pop_cv.wait(
        lock,
        [this]
        {
            return !_events_queue.empty() || GetState() == TStateMachineState::Stopped;
        }
    );

    const TStateMachineState cached_state = GetState();

    if (cached_state == TStateMachineState::Stopped)
    {
        return std::unexpected{ QueueTypes::Error::queue_not_started_or_shut_down };
    }

    const bool was_full = _events_queue.full();
    const auto retval_span = _events_queue.pop_into(destination);
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
