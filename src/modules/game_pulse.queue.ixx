module;

#include "hamed_common/generic_types.h"

export module game_pulse.queue;

import game_pulse.domain;

import <atomic>;
import <cstddef>;
import <expected>;
import <span>;

template<typename T>
concept NothrowQueuePayload =
    std::is_object_v<T> &&
    std::destructible<T> &&
    std::is_nothrow_move_constructible_v<T> &&
    std::is_nothrow_move_assignable_v<T>;

export
{

    namespace QueueTypes
    {
        enum class Error
        {
            bad_arguments = 0,
            internal_error,
            queue_not_started_or_shut_down,
        };
    }

    class Queue final : public TStateMachine<>
    {
    public:

        explicit Queue(const std::size_t queue_capacity);

        [[nodiscard]] std::size_t GetSize() const;
        [[nodiscard]] std::size_t GetCapacity() const noexcept { return _events_queue.capacity(); }

        std::expected<void, QueueTypes::Error> WaitAndPush(EventTypes::Event event);
        std::expected<std::span<EventTypes::Event>, QueueTypes::Error> WaitAndPopBatch(std::span<EventTypes::Event> destination);

    protected:
        
        virtual void OnStateTransitionLocked(const TStateMachineState newState) noexcept override;
        virtual void OnStateTransitionUnlocked(const TStateMachineState newState) noexcept override;

    private:

        static_assert(NothrowQueuePayload<EventTypes::Event> && std::is_trivially_copyable_v<EventTypes::Event>, "Queue requires a trivially copyable & nothrow-movable Event payload.");

        std::condition_variable _queue_push_cv;
        std::condition_variable _queue_pop_cv;

        TRingQueue<EventTypes::Event> _events_queue;

    };

}
