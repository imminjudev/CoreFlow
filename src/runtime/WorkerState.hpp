#pragma once

#include <cstddef>
#include <thread>

#include "core/CoreBlockingDeque.hpp"
#include "runtime/Task.hpp"

struct WorkerState
{
    std::size_t id = 0;

    std::thread thread;

    CoreBlockingDeque<Task> localQueue;
};