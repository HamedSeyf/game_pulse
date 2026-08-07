module game_pulse.queue;

import <cassert>;
import <stdexcept>;

Queue::Queue(const std::size_t queue_capacity)
    : _events_queue { queue_capacity }
{
    if (queue_capacity == 0)
    {
        throw std::invalid_argument{ "Queue capacity must be greater than zero." };
    }
}

std::size_t Queue::GetCurrentSize() const
{
    std::lock_guard<std::mutex> lock(_queue_and_state_mutex);
    return _events_queue.size();
}

bool Queue::ShutDown(const bool graceful)
{
    {
        std::lock_guard<std::mutex> lock(_queue_and_state_mutex);

        if (GetState() != State::ready)
        {
            assert(false && "Multiple Queue::ShutDown called.");
            return false;
        }

        if (!graceful)
        {
            _events_queue.clear();
        }

        const State new_state = (_events_queue.empty() ? State::shut_down : State::shutting_down_gracefully);
            
        _state.store(new_state, std::memory_order_relaxed);
    }

    _queue_push_cv.notify_all();
    _queue_pop_cv.notify_all();

    return true;
}

std::expected<void, Queue::QueueError> Queue::WaitAndPush(Event event)
{
    {
        std::unique_lock<std::mutex> lock(_queue_and_state_mutex);

        _queue_push_cv.wait(
            lock,
            [this]
            {
                return !_events_queue.full() || GetState() != State::ready;
            }
        );

        if (GetState() != State::ready)
        {
            return std::unexpected{ QueueError::already_shut_down };
        }

        if (!_events_queue.try_emplace(std::move(event)))
        {
            assert(false && "Broken internal logic as _events_queue should not be full and std::move should work on event objects.");
            return std::unexpected{ QueueError::internal_error };
        }
    }

    _queue_pop_cv.notify_one();

    return {};
}

std::expected<std::span<Event>, Queue::QueueError> Queue::WaitAndPopBatch(std::span<Event> destination)
{
    if (destination.size() == 0)
    {
        return std::unexpected{ QueueError::bad_arguments };
    }

    std::unique_lock lock{ _queue_and_state_mutex };

    _queue_pop_cv.wait(
        lock,
        [this]
        {
            return !_events_queue.empty() || GetState() != State::ready;
        }
    );

    const State cached_state = GetState();

    if (cached_state == State::shut_down)
    {
        return std::unexpected{ QueueError::already_shut_down };
    }

    const bool was_full = _events_queue.full();

    assert(!_events_queue.empty() && "State transition and mutex lock logic should prevent this scenario to occur.");

    const auto retval_span = _events_queue.pop_into(destination);

    if (cached_state == State::shutting_down_gracefully && _events_queue.empty())
    {
        _state.store(State::shut_down, std::memory_order_relaxed);
    }

    lock.unlock();

    // Check whether or not we should notify all waiters
    if (cached_state == State::ready && was_full)
    {
        _queue_push_cv.notify_all();
    }

    return retval_span;
}
