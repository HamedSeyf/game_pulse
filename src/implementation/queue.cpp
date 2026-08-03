module game_pulse.queue;

import <cassert>;

size_t Queue::GetCurrentSize() const
{
    std::lock_guard<std::mutex> lock(_queue_mutex);
    return _events_queue.size();
}

bool Queue::Cofigure(const std::shared_ptr<const Configuration>& config)
{
    assert(config != nullptr && GetCapacity() == 0);

    {
        std::lock_guard<std::mutex> lock(_queue_mutex);
        _queue_capacity.store(config->queue_capacity);
    }

    assert(GetCapacity() > 0);

    return true;
}

void Queue::ShutDown(const bool graceful)
{
    assert(GetState() == State::ready);

    {
        // TODO: Question: is this lock needed and in which scenarios if yes?
        std::lock_guard<std::mutex> lock(_queue_mutex);

        State new_state =
            _events_queue.empty() ? State::shut_down : (graceful ? State::shutting_down_gracefully : State::shutting_down_ungracefully);
            
        _state.store(new_state, std::memory_order_relaxed);
    }

    _queue_push_cv.notify_all();
    _queue_pop_cv.notify_all();
}

std::expected<bool, std::string> Queue::WaitAndPush(Event event)
{
    std::unique_lock<std::mutex> lock(_queue_mutex);

    if (GetState() != State::ready)
    {
        return std::unexpected{ std::string{"Queue either shutting down or already shut down"} };
    }

    _queue_push_cv.wait(
        lock,
        [this]
        {
            return _events_queue.size() < _queue_capacity.load(std::memory_order_relaxed) || GetState() != State::ready;
        }
    );

    if (GetState() != State::ready)
    {
        return std::unexpected{ std::string{"Queue either shutting down or already shut down"} };
    }

    _events_queue.emplace(std::move(event));

    _queue_pop_cv.notify_one();
    
    return true;
}

std::expected<std::unique_ptr<Event>, std::string> Queue::WaitAndPop()
{
    std::unique_lock lock{ _queue_mutex };

    if (const auto state = GetState(); state == State::shutting_down_ungracefully || state == State::shut_down)
    {
        return std::unexpected{ std::string{"Queue either shutting down or already shut down"} };
    }

    _queue_pop_cv.wait(
        lock,
        [this]
        {
            return !_events_queue.empty() || GetState() != State::ready;
        }
    );

    if (const auto state = GetState(); state == State::shutting_down_ungracefully || state == State::shut_down)
    {
        return std::unexpected{ std::string{"Queue either shutting down or already shut down"} };
    }

    if (_events_queue.empty())
    {
        if (const auto state = GetState(); state == State::shutting_down_gracefully)
        {
            _state.store(State::shut_down);
        }
        return;
    }

    auto front_event = std::move(_events_queue.front());
    _events_queue.pop();

    _queue_push_cv.notify_one();

    if (GetState() == State::shutting_down_gracefully && _events_queue.empty())
    {
        _state.store(State::shut_down);
    }

    return front_event;
}
