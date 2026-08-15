module;

#include "hamed_common/subscription_registry.h"

export module game_pulse.pipeline;

import game_pulse.domain;
import game_pulse.queue;

import <string>;
import <expected>;
import <memory>;
import <span>;

export
{

    class ProcessorInterface
    {
    public:
        // Processes events synchronously.
        // `events`, and any pointer/reference derived from it, are valid only for the
        // duration of this call. A processor that needs the events afterward must copy
        // them into processor-owned storage before returning.
        virtual void ProcessEventsSynchronously(const std::span<const Event>& events) = 0;
        virtual ~ProcessorInterface() = default;
    };

    class Pipeline final
    {
    public:

        enum class PipelineState
        {
            NotStarted = 0,
            InProgress,
            Finishing_Gracefully,
            Stopped,
        };

        enum class PipelineError
        {
            UnRegisterFailed = 0,
            PipelineAlreadyStarted,
            PipelineNotYetStarted,
        };

        using TProsessorHandle = std::uint64_t;

        explicit Pipeline(std::shared_ptr<Queue> queue, const std::size_t batch_size);

        std::expected<void, PipelineError> Start();
        std::expected<void, PipelineError> Stop(const bool graceful);

        [[nodiscard]] PipelineState GetState() const noexcept { return _state->load(std::memory_order_relaxed); }

        std::expected<TProsessorHandle, PipelineError> RegisterProsessor(std::shared_ptr<ProcessorInterface> processor);
        std::expected<void, PipelineError> UnRegisterProsessor(const TProsessorHandle& handle);

    private:

        std::shared_ptr<Queue> _queue;
        size_t _batch_size;

        inline static constexpr std::string_view SubscriptionRegistryKey = "PipelineEvents";

        TSubscriptionRegistry<std::shared_ptr<ProcessorInterface>, std::string_view, TProsessorHandle> _subscriptionRegistry;

        std::mutex _state_mutex;
        std::shared_ptr<std::atomic<PipelineState>> _state;

        std::jthread _workerThread;

        void WorkerMain(std::stop_token stop_token);
    };
}
