module;

#include "hamed_common/generic_singleton.h"

export module game_pulse.queue;

import game_pulse.domain;

import <expected>;
import <queue>;
import <memory>;
import <mutex>;

export
{

    class Queue : public TSingleton<Queue>
    {
        ENFORCE_SINGELTON_WITH_DEFAULT_CTOR(Queue);
    public:

        enum class State
        {
            ready = 0,
            shutting_down_gracefully,
            shutting_down_ungracefully,
            shut_down,
        };

        size_t GetCurrentSize() const;
        __forceinline size_t GetCapacity() const { return _queue_capacity.load(std::memory_order_relaxed); }
        __forceinline State GetState() const { return _state.load(std::memory_order_relaxed); }

        bool Cofigure(const std::shared_ptr<const Configuration>& config);
        void ShutDown(const bool graceful);

        std::expected<bool, std::string> WaitAndPush(Event event);
        std::expected<std::unique_ptr<Event>, std::string> WaitAndPop();

    private:

        std::atomic<size_t> _queue_capacity { 0 };
        std::atomic<State> _state { State::ready };

        mutable std::mutex _queue_mutex;
        std::condition_variable _queue_push_cv;
        std::condition_variable _queue_pop_cv;
        std::queue<std::unique_ptr<Event>> _events_queue;

    };

}
