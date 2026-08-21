module;

#include "hamed_common/generic_types.h"

export module game_pulse.analytics;

import game_pulse.pipeline;

import <atomic>;
import <expected>;
import <memory>;

export
{

    namespace AnalyticsType
    {
        struct AnalyticsSnapshot
        {

        };
    }

    class Analytics : public TStateMachine<>, public PipelineTypes::ProcessorInterface
    {
    public:

        virtual [[nodiscard]] AnalyticsType::AnalyticsSnapshot GetSnapshot() const;

        // PipelineTypes::ProcessorInterface override(s)
        virtual void ProcessEventsSynchronously(const std::span<const EventTypes::Event>& events) override;

    protected:

        virtual void OnStateTransitionLocked(const TStateMachineState newState) noexcept override;

    private:

        std::jthread _workerThread;

        void WorkerMain(std::stop_token stopToken);
    };

}
