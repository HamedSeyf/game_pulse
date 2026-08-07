module;

#include "hamed_common/generic_types.h"

export module game_pulse.queue;

import game_pulse.domain;

import <atomic>;
import <condition_variable>;
import <cstddef>;
import <expected>;
import <mutex>;
import <span>;

template<typename T>
concept NothrowQueuePayload =
    std::is_object_v<T> &&
    std::destructible<T> &&
    std::is_nothrow_move_constructible_v<T> &&
    std::is_nothrow_move_assignable_v<T>;

export
{

    class Queue final
    {
    public:

        enum class State
        {
            ready = 0,
            shutting_down_gracefully,
            shut_down,
        };

        enum class QueueError
        {
            bad_arguments = 0,
            already_shut_down,
            internal_error,
        };

        explicit Queue(const std::size_t queue_capacity);

        [[nodiscard]] std::size_t GetCurrentSize() const;
        [[nodiscard]] std::size_t GetCapacity() const noexcept { return _events_queue.capacity(); }
        [[nodiscard]] State GetState() const noexcept { return _state.load(std::memory_order_relaxed); }

        bool ShutDown(const bool graceful);

        std::expected<void, QueueError> WaitAndPush(Event event);
        std::expected<std::span<Event>, QueueError> WaitAndPopBatch(std::span<Event> destination);

    private:

        static_assert(NothrowQueuePayload<Event>, "Queue requires a nothrow-movable Event payload.");

        // _state is atomic to support cheap external snapshots such as GetState() without lock; However, its changes are done under _queue_and_state_mutex lock
        std::atomic<State> _state { State::ready };

        mutable std::mutex _queue_and_state_mutex;
        std::condition_variable _queue_push_cv;
        std::condition_variable _queue_pop_cv;

        TRingQueue<Event> _events_queue;

    };

}
