export module game_pulse.domain;

import <cstddef>;
import <cstdint>;
import <tuple>;

export
{

    using T_ID = std::uint64_t;

    using T_Position = std::tuple<double, double, double>;

    enum class Priority
    {
        highest = 0,
        high,
        medium,
        low,
        lowest
    };

    struct Event
    {
        T_ID id;
        std::uint64_t tick;
        Priority priority;
    };

    struct Configuration final
    {
        size_t queue_capacity = 200;
        size_t batch_size = 10;
        size_t producer_count = 5;
        size_t snapshot_interval = 500; // milliseconds
        bool shutdown_gracefully = true;
    };

}
