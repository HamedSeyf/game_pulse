module game_pulse.analytics;

import <cassert>;
import <iostream>;


AnalyticsType::AnalyticsSnapshot Analytics::GetSnapshot() const
{
    std::shared_lock lock(_mutex);
    return AnalyticsType::AnalyticsSnapshot{ _playersStatus };
}

void Analytics::ProcessEventsSynchronously(const std::span<const EventTypes::Event>& events)
{
    if (events.empty())
    {
        return;
    }
    
    std::unique_lock<std::shared_mutex> lock(_mutex);

    for (const auto& currentEvent : events)
    {
        switch (currentEvent.type)
        {
        case EventTypes::EventType::Spawn:
        {
            const auto foundPlayer = _playersStatus.find(currentEvent.spawn.playerId);
            if (foundPlayer == _playersStatus.end() || foundPlayer->second.health == 0)
            {
                try
                {
                    _playersStatus[currentEvent.spawn.playerId] = { PlayerMaxHealth , currentEvent.spawn.position };
                }
                catch (const std::exception& e)
                {
                    assert(false && "Allocation failed inside Analytics::ProcessEventsSynchronously.");
                    // For now we keep it to a simple cerr as proper logging is not within the scope of this code demonstration
                    std::cerr << e.what() << '\n';
                }
                catch (...)
                {
                    assert(false && "Allocation failed inside Analytics::ProcessEventsSynchronously.");
                    // For now we keep it to a simple cerr as proper logging is not within the scope of this code demonstration
                    std::cerr << "Unknown non-std::exception thrown inside Analytics::ProcessEventsSynchronously.\n";
                }
            }
            break;
        }
        case EventTypes::EventType::Move:
        {
            const auto foundPlayer = _playersStatus.find(currentEvent.move.playerId);
            if (foundPlayer != _playersStatus.end() && foundPlayer->second.health > 0)
            {
                foundPlayer->second.position = currentEvent.move.position;
            }
            break;
        }
        case EventTypes::EventType::Shot:
        {
            const auto foundPlayer_Shooter = _playersStatus.find(currentEvent.shot.shooterId);
            const auto foundPlayer_Target = _playersStatus.find(currentEvent.shot.targetId);
            if (foundPlayer_Shooter != _playersStatus.end() && foundPlayer_Shooter->second.health > 0 &&
                foundPlayer_Target != _playersStatus.end() && foundPlayer_Target->second.health > 0)
            {
                foundPlayer_Target->second.health -= std::min(foundPlayer_Target->second.health, currentEvent.shot.damage);
            }
            break;
        }
        default:
            assert(false && "Unsupported event type passed to Analytics::ProcessEventsSynchronously.");
            break;
        }
    }
}
