module game_pulse.analytics;

import <iostream>;


AnalyticsType::AnalyticsSnapshot Analytics::GetSnapshot() const
{
    return AnalyticsType::AnalyticsSnapshot();
}

void Analytics::ProcessEventsSynchronously([[maybe_unused]] const std::span<const EventTypes::Event>& events)
{
}

void Analytics::OnStateTransitionLocked(const TStateMachineState newState) noexcept
{
    TStateMachine::OnStateTransitionLocked(newState);

    try
    {
        if (newState == TStateMachineState::InProgress)
        {
            _workerThread = std::jthread([this](std::stop_token stopToken)
                {
                    WorkerMain(stopToken);
                }
            );
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    catch (...)
    {
        std::cerr << "Unknown non-std::exception thrown inside Analytics::OnStateTransitionUnlocked.\n";
    }
}

void Analytics::WorkerMain(std::stop_token stopToken)
{
    while (!stopToken.stop_requested())
    {
    }
}
