module;

#include "hamed_common/generic_types.h"

export module game_pulse.simulator;

import game_pulse.domain;
import game_pulse.queue;

import <memory>;
import <random>;
import <span>;
import <stop_token>;
import <thread>;
import <vector>;


export
{

    namespace SimulatorTypes
    {
        enum class TSimulatorStateMachineState
        {
            NotStarted = 0,
            InProgress,
            Stopped,
        };

        struct TEventGenerationWeights final
        {
            double spawnWeight{};
            double moveWeight{};
            double shotWeight{};
            double noEventWeight{};
        };
    }

    class Simulator :
        public TStateMachine<SimulatorTypes::TSimulatorStateMachineState>,
        public QueueTypes::SimulatorInterface,
        public std::enable_shared_from_this<Simulator>
    {
    public:

        explicit Simulator(
            std::shared_ptr<TickClock> tickClock,
            std::shared_ptr<Queue> queue,
            T_ID playerID,
            const std::span<T_ID> otherPlayerIDs,
            SimulatorTypes::TEventGenerationWeights eventGenerationWeights,
            std::uint64_t randomSeed);

        // TODO: added this so to make sure the recently added queue handle is not copyable. Anything missing?
        Simulator(const Simulator&) = delete;

        // TODO: what do you think of this operator quality?
        virtual bool operator==([[maybe_unused]] const QueueTypes::SimulatorInterface& other) const override { return true; }// TODO _playerID == other._playerID && _queueRegistrationHandle == other._queueRegistrationHandle;

    protected:

        void OnStateTransitionLocked(const SimulatorTypes::TSimulatorStateMachineState newState) noexcept override;

    private:

        struct TEventGenerationCutoffs final
        {
            double spawnEnd;
            double moveEnd;
            double shotEnd;
        };

        const T_ID _playerID;
        const std::vector<T_ID> _otherPlayerIDs;

        std::shared_ptr<TickClock> _tickClock;
            
        std::shared_ptr<Queue> _queue;

        // TODO: need to cover unregister from queue and setting this back to null
        std::optional<Queue::TSimulatorHandle> _queueRegistrationHandle = std::nullopt;

        const TEventGenerationCutoffs _eventGenerationCutoffs;

        std::mt19937_64 _randomEngine;
        std::uniform_real_distribution<double> _unitDistribution{ 0.0, 1.0 };
        std::uniform_int_distribution<std::size_t> _targetDistribution;
        std::uniform_int_distribution<TPlayerHealthType> _damageDistribution;

        std::jthread _workerThread;

        void WorkerMain(std::stop_token stopToken);

        [[nodiscard]] static TEventGenerationCutoffs BuildEventGenerationCutoffs(const SimulatorTypes::TEventGenerationWeights& weights);
        [[nodiscard]] std::optional<EventTypes::Event> CreateRandomEvent(const TickClock::Tick tick);

    };
}
