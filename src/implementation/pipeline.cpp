module game_pulse.pipeline;

import <cassert>;
import <iostream>;

Pipeline::Pipeline(std::shared_ptr<Queue> queue, const std::size_t batch_size)
    : _queue(std::move(queue)), _batch_size(batch_size)
{
    if (!_queue || _batch_size == 0)
    {
        throw std::invalid_argument{ "Invalid Queue passed to Pipeline's ctor." };
    }
    assert(_batch_size <= _queue->GetCapacity() && "Batch size bigger than queue's capacity is not useful.");
}

Pipeline::~Pipeline()
{
    SwitchToState(TStateMachineState::Stopped);
}

std::expected<Pipeline::TProcessorHandle, PipelineTypes::Error> Pipeline::RegisterProcessor(std::shared_ptr<PipelineTypes::ProcessorInterface> processor)
{
    const TProcessorHandle subscriptionHandle = _subscriptionRegistry.subscribe(processor, Pipeline::SubscriptionRegistryKey);
    return { subscriptionHandle };
}

std::expected<void, PipelineTypes::Error> Pipeline::UnRegisterProcessor(const TProcessorHandle& handle)
{
    const bool success = _subscriptionRegistry.unsubscribe(handle);
    return success ? std::expected<void, PipelineTypes::Error>{} : std::unexpected{ PipelineTypes::Error::UnRegisterFailed };
}

void Pipeline::JoinAndWait()
{
    if (_workerThread.joinable())
    {
        _workerThread.join();
    }
}

void Pipeline::OnStateTransitionLocked(const TStateMachineState newState) noexcept
{
    TStateMachine::OnStateTransitionLocked(newState);

    try
    {
        if (newState == TStateMachineState::InProgress)
        {
            _queue->SwitchToState(TStateMachineState::InProgress);

            _workerThread = std::jthread([this](std::stop_token stopToken)
                {
                    WorkerMain(stopToken);
                }
            );
        }
        else if (newState == TStateMachineState::Stopping_Gracefully)
        {
            _queue->SwitchToState(TStateMachineState::Stopping_Gracefully);
        }
        else if (newState == TStateMachineState::Stopped)
        {
            _queue->SwitchToState(TStateMachineState::Stopped);

            _workerThread.request_stop();
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    catch (...)
    {
        std::cerr << "Unknown non-std::exception thrown inside Pipeline::OnStateTransitionLocked.\n";
    }
}

void Pipeline::WorkerMain(std::stop_token stopToken)
{
    std::vector<EventTypes::Event> eventsBatchBuffer(_batch_size, {});

    while (!stopToken.stop_requested() || (GetState() == TStateMachineState::Stopping_Gracefully))
    {
        const auto expectedEvents = _queue->WaitAndPopBatch(eventsBatchBuffer);

        if (!expectedEvents)
        {
            switch (expectedEvents.error())
            {
            case QueueTypes::Error::bad_arguments:
                assert(false && "Pipeline passed invalid arguments to Queue::WaitAndPopBatch.");
                break;

            case QueueTypes::Error::queue_not_started_or_shut_down:
                break;

            case QueueTypes::Error::internal_error:
                assert(false && "Queue::WaitAndPopBatch encountered an internal error.");
                break;
            }

            break;
        }

        const auto subscribers = _subscriptionRegistry.getSubscribedObjects(Pipeline::SubscriptionRegistryKey);

        for (auto& currentSubscriber : subscribers)
        {
            try
            {
                currentSubscriber->ProcessEventsSynchronously(expectedEvents.value());
            }
            catch (const std::exception& e)
            {
                assert(false && "Subscribers are supposed to gracefully handle events without throwing.");
                // For now we keep it to a simple cerr as proper logging is not within the scope of this code demonstration
                std::cerr << e.what() << '\n';
            }
            catch (...)
            {
                assert(false && "Subscribers are supposed to gracefully handle events without throwing.");
                // For now we keep it to a simple cerr as proper logging is not within the scope of this code demonstration
                std::cerr << "Unknown non-std::exception thrown by subscriber.\n";
            }
        }
    }

    if (GetState() != TStateMachineState::Stopped)
    {
        if (const auto stopResult = SwitchToState(TStateMachineState::Stopped); !stopResult)
        {
            assert(false && "Pipeline failed to transition to Stopped state on its final thread exit.");
        }
    }
}
