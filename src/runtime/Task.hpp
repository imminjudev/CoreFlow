#pragma once

#include <cstdint>


enum class TaskType
{
    Sleep,
    Compute
};


struct Task
{
    int id;

    const char* name;

    TaskType type;

    // Sleep Task:
    //     millisecond
    //
    // Compute Task:
    //     iteration count
    std::uint64_t workAmount;
};