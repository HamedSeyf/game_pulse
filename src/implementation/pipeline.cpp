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

std::expected<void, Pipeline::PipelineError> Pipeline::Start()
{
    std::lock_guard<std::mutex> lock(_state_mutex);

    if (GetState() == PipelineState::InProgress)
    {
        return std::unexpected{ PipelineError::PipelineAlreadyStarted };
    }

    _state->store(PipelineState::InProgress);

    _workerThread = std::jthread([this](std::stop_token stopToken)
        {
            WorkerMain(stopToken);
        }
    );

    return {};
}

std::expected<void, Pipeline::PipelineError> Pipeline::Stop([[maybe_unused]] const bool graceful)
{
    std::lock_guard<std::mutex> lock(_state_mutex);

    if (GetState() != PipelineState::InProgress)
    {
        return std::unexpected{ PipelineError::PipelineNotYetStarted };
    }

    if (graceful)
    {
        _state->store(PipelineState::Finishing_Gracefully);
    }
    else
    {
        _workerThread.request_stop();
    }

    return {};
}

std::expected<Pipeline::TProsessorHandle, Pipeline::PipelineError> Pipeline::RegisterProsessor(std::shared_ptr<ProcessorInterface> processor)
{
    const TProsessorHandle subscriptionHandle = _subscriptionRegistry.subscribe(processor, Pipeline::SubscriptionRegistryKey);
    return { subscriptionHandle };
}

std::expected<void, Pipeline::PipelineError> Pipeline::UnRegisterProsessor(const TProsessorHandle& handle)
{
    const bool success = _subscriptionRegistry.unsubscribe(handle);
    // TODO: question for Codex, are we handling potential errors and/or exceptions well in this func?
    return success ? std::expected<void, Pipeline::PipelineError>{} : std::unexpected{ Pipeline::PipelineError::UnRegisterFailed };
}

void Pipeline::WorkerMain(std::stop_token stopToken)
{
    std::vector<Event> eventsBatchBuffer(_batch_size, {});

    while (!stopToken.stop_requested() || (GetState() == PipelineState::Finishing_Gracefully))
    {
        const auto expectedEvents = _queue->WaitAndPopBatch(eventsBatchBuffer);

        if (!expectedEvents)
        {
            switch (expectedEvents.error())
            {
            case Queue::QueueError::bad_arguments:
                assert(false && "Pipeline passed invalid arguments to Queue::WaitAndPopBatch.");
                break;

            case Queue::QueueError::already_shut_down:
                break;

            case Queue::QueueError::internal_error:
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
        }
    }

    {
        std::lock_guard<std::mutex> lock(_state_mutex);
        _state->store(PipelineState::Stopped);
    }
}
