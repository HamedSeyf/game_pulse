module;

#include "hamed_common/generic_types.h"
#include "hamed_common/subscription_registry.h"

export module game_pulse.pipeline;

import game_pulse.domain;
import game_pulse.queue;

import <string>;
import <expected>;
import <memory>;
import <span>;
import <thread>;
import <stop_token>;

export
{

    namespace PipelineTypes
    {
        class ProcessorInterface
        {
        public:
            // Processes events synchronously.
            // `events`, and any pointer/reference derived from it, are valid only for the
            // duration of this call. A processor that needs the events afterward must copy
            // them into processor-owned storage before returning.
            virtual void ProcessEventsSynchronously(const std::span<const EventTypes::Event>& events) = 0;
            virtual ~ProcessorInterface() = default;
        };

        enum class Error
        {
            UnRegisterFailed = 0,
        };
    }

    class Pipeline final : public TStateMachine<>
    {
    public:

        using TProcessorHandle = std::uint64_t;

        explicit Pipeline(std::shared_ptr<Queue> queue, const std::size_t batch_size);
        ~Pipeline();

        std::expected<TProcessorHandle, PipelineTypes::Error> RegisterProcessor(std::shared_ptr<PipelineTypes::ProcessorInterface> processor);
        std::expected<void, PipelineTypes::Error> UnRegisterProcessor(const TProcessorHandle& handle);

        void JoinAndWait();

    protected:
        
        virtual void OnStateTransitionLocked(const TStateMachineState newState) noexcept override;

    private:

        std::shared_ptr<Queue> _queue;
        std::size_t _batch_size;

        inline static constexpr std::string_view SubscriptionRegistryKey = "PipelineEvents";

        TSubscriptionRegistry<std::shared_ptr<PipelineTypes::ProcessorInterface>, std::string_view, TProcessorHandle> _subscriptionRegistry;

        std::jthread _workerThread;

        void WorkerMain(std::stop_token stopToken);
    };
}
