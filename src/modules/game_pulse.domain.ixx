export module game_pulse.domain;

import <array>;
import <cstddef>;
import <cstdint>;
import <tuple>;

export
{

    using T_ID = std::uint64_t;
    using T_Tick = std::uint64_t;
    using TPlayerHealthType = uint64_t;
    using TPlayerPositionType = std::array<double, 2>;
    inline constexpr TPlayerHealthType PlayerMaxHealth = 10000;

    namespace EventTypes
    {
        enum class EventType : std::uint8_t
        {
            Spawn,
            Move,
            Shot,
            Death
        };

        struct SpawnEvent
        {
            T_ID playerId;
            TPlayerPositionType position;
        };

        struct MoveEvent
        {
            T_ID playerId;
            TPlayerPositionType position;
        };

        struct ShotEvent
        {
            T_ID shooterId;
            T_ID targetId;
            TPlayerHealthType damage;
        };

        struct DeathEvent
        {
            T_ID playerId;
            T_ID killerId;
        };

        struct Event
        {
            T_ID id;
            T_Tick tick;
            EventType type;

            union
            {
                SpawnEvent spawn;
                MoveEvent move;
                ShotEvent shot;
                DeathEvent death;
            };
        };
    }

    namespace SnapshotTypes
    {
        struct PlayerStatus
        {
            TPlayerHealthType Health = PlayerMaxHealth;
            TPlayerPositionType Position{ 0.0, 0.0 };
        };
    }

    struct Configuration final
    {
        size_t queue_capacity = 200;
        size_t batch_size = 10;
        size_t producer_count = 5;
        size_t snapshot_interval = 500; // milliseconds
        bool shutdown_gracefully = true;
    };

}
