module;

#include "hamed_common/platform.h"

#include <spdlog/spdlog.h>

module game_pulse.pipeline;

import game_pulse.queue;

import <algorithm>;
import <cassert>;
import <tuple>;


Pipeline::Pipeline(std::shared_ptr<TickClock> tickClock, std::shared_ptr<Queue> queue, const std::size_t batchSize)
    : _tickClock(std::move(tickClock)), _queue(std::move(queue)), _batch_size(batchSize)
{
    if (!_tickClock || !_queue || _batch_size == 0)
    {
        throw std::invalid_argument{ "Invalid tickClock, queue or batchSize passed to Pipeline's ctor." };
    }
    assert(_batch_size <= _queue->GetCapacity() && "Batch size bigger than queue's capacity is not useful.");
}

Pipeline::~Pipeline()
{
    SwitchToState(TStateMachineState::Stopped);
}

std::expected<Pipeline::TProcessorHandle, PipelineTypes::Error> Pipeline::RegisterProcessor(std::shared_ptr<PipelineTypes::ProcessorInterface> processor)
{
    if (!processor)
    {
        return std::unexpected{ PipelineTypes::Error::bad_arguments };
    }

    TProcessorHandle registeredHandle{};

    {
        std::unique_lock lock{ _state_mutex };

        const bool foundElement = _subscriptionRegistry.forEachSubscribedObject(Pipeline::SubscriptionRegistryKey, [&processor](const auto& currentProcessor)
            {
                return (currentProcessor == processor);
            });

        if (foundElement)
        {
            return std::unexpected{ PipelineTypes::Error::processor_already_registered };
        }

        registeredHandle = _subscriptionRegistry.subscribe(processor, Pipeline::SubscriptionRegistryKey);
    }

    spdlog::info("Pipeline successfully registered processor.");

    return { std::move(registeredHandle) };
}

std::expected<void, PipelineTypes::Error> Pipeline::UnRegisterProcessor(const TProcessorHandle& handle)
{
    const bool success = _subscriptionRegistry.unsubscribe(handle);
    spdlog::info("Pipeline's unregister call result: {} Handle: {}", success, handle);
    return success ? std::expected<void, PipelineTypes::Error>{} : std::unexpected{ PipelineTypes::Error::processor_not_registered };
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
    spdlog::info("Pipeline transitioned to new state. State: {}", std::to_underlying(newState));

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
        else if (newState == TStateMachineState::Stopped)
        {
            _workerThread.request_stop();
        }
    }
    catch (const std::exception& e)
    {
        spdlog::error("{}", e.what());
    }
    catch (...)
    {
        spdlog::error("Unknown non-std::exception thrown inside Pipeline::OnStateTransitionLocked.");
    }
}

void Pipeline::WorkerMain(std::stop_token stopToken)
{
    std::vector<EventTypes::Event> eventsBatchBuffer(_batch_size, {});

    while (!stopToken.stop_requested() || (GetState() == TStateMachineState::Stopping_Gracefully))
    {
        const auto expectedEvents = _queue->WaitAndPop(eventsBatchBuffer, _tickClock->GetCurrentTick(), stopToken);

        if (!expectedEvents)
        {
            switch (expectedEvents.error())
            {
            case QueueTypes::Error::bad_arguments:
                assert(false && "Pipeline passed invalid arguments to Queue::WaitAndPopBatch.");
                break;

            case QueueTypes::Error::internal_error:
                assert(false && "Queue::WaitAndPopBatch encountered an internal error.");
                break;

            case QueueTypes::Error::queue_not_started_or_shut_down:
                assert(false && "Broken logic and contract between Pipeline & Queue: queue's state changes should initiate and hense be synced with the pipeline.");
                break;

            case QueueTypes::Error::operation_cancelled:
                assert(stopToken.stop_requested() && "Broken logic and contract between Pipeline & Queue.");
                break;

            default:
                assert(false && "Unsupported QueueTypes::Error found inside Pipeline::WorkerMain.");
                break;
            }

            break;
        }

        spdlog::debug("Pipeline successfully popped {} events from the queue.", expectedEvents.value().size());

        std::sort(expectedEvents.value().begin(), expectedEvents.value().end(), [](const EventTypes::Event& lEvent, const EventTypes::Event& rEvent)
            {
                return std::tie(lEvent.tick, lEvent.id) < std::tie(rEvent.tick, rEvent.id);
            });

        const auto subscribers = _subscriptionRegistry.getSubscribedObjects(Pipeline::SubscriptionRegistryKey);

        if (!subscribers)
        {
            spdlog::error("Failed to fetch subscribers' list inside Pipeline::WorkerMain. Exitting pipeline loop.");
            assert(false && "Failed to fetch subscribers' list inside Pipeline::WorkerMain. Exitting pipeline loop.");
            break;
        }

        for (auto& currentSubscriber : subscribers.value())
        {
            try
            {
                currentSubscriber->ProcessEventsSynchronously(expectedEvents.value());
            }
            catch (const std::exception& e)
            {
                spdlog::error("{}", e.what());
                assert(false && "Subscribers are supposed to gracefully handle events without throwing.");
            }
            catch (...)
            {
                spdlog::error("Unknown non-std::exception thrown by subscriber.");
                assert(false && "Subscribers are supposed to gracefully handle events without throwing.");
            }
        }

        HAMEDSEYF_CPU_RELAX();
    }

    if (GetState() != TStateMachineState::Stopped)
    {
        if (const auto stopResult = SwitchToState(TStateMachineState::Stopped); !stopResult)
        {
            assert(false && "Pipeline failed to transition to Stopped state on its final thread exit.");
        }
    }
}
