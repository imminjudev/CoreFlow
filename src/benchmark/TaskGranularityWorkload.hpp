#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/Task.hpp"


// ---------------------------------------------------------
// TaskGranularityWorkload
//
// 전체 CPU 연산량은 일정하게 유지하면서
// Task 개수만 변경하는 Benchmark Workload.
//
// 예:
//
// totalIterations = 1,000,000,000
//
// 8 Tasks
//     -> Task당 125,000,000 iterations
//
// 40 Tasks
//     -> Task당 25,000,000 iterations
//
// 1000 Tasks
//     -> Task당 1,000,000 iterations
//
// 이를 통해 Task Granularity가 Scheduler 성능에
// 어떤 영향을 주는지 측정한다.
// ---------------------------------------------------------
class TaskGranularityWorkload
{
private:
    Task* m_tasks;

    std::size_t m_taskCount;

    std::uint64_t m_totalIterations;

    std::uint64_t m_baseIterationsPerTask;


public:
    TaskGranularityWorkload(
        std::size_t taskCount,
        std::uint64_t totalIterations
    )
        : m_tasks(nullptr),
          m_taskCount(taskCount),
          m_totalIterations(totalIterations),
          m_baseIterationsPerTask(0)
    {
        if (m_taskCount == 0)
        {
            return;
        }


        m_tasks =
            new Task[m_taskCount];


        // -------------------------------------------------
        // 기본 Task당 연산량
        // -------------------------------------------------
        m_baseIterationsPerTask =
            m_totalIterations
            /
            static_cast<std::uint64_t>(
                m_taskCount
            );


        // -------------------------------------------------
        // 나머지 iterations
        //
        // totalIterations가 taskCount로 정확히
        // 나누어지지 않는 경우 앞쪽 Task들에게
        // 1 iteration씩 추가한다.
        //
        // 따라서 전체 연산량은 항상 정확히
        // totalIterations가 된다.
        // -------------------------------------------------
        std::uint64_t remainder =
            m_totalIterations
            %
            static_cast<std::uint64_t>(
                m_taskCount
            );


        for (
            std::size_t i = 0;
            i < m_taskCount;
            i++
        )
        {
            std::uint64_t workAmount =
                m_baseIterationsPerTask;


            if (
                static_cast<std::uint64_t>(i)
                < remainder
            )
            {
                workAmount++;
            }


            m_tasks[i] =
                Task{
                    static_cast<int>(i + 1),
                    "GranularityComputeTask",
                    TaskType::Compute,
                    workAmount
                };
        }
    }


    ~TaskGranularityWorkload()
    {
        delete[] m_tasks;

        m_tasks =
            nullptr;
    }


    TaskGranularityWorkload(
        const TaskGranularityWorkload&
    ) = delete;


    TaskGranularityWorkload& operator=(
        const TaskGranularityWorkload&
    ) = delete;


    const Task* tasks() const
    {
        return m_tasks;
    }


    std::size_t taskCount() const
    {
        return m_taskCount;
    }


    std::uint64_t totalIterations() const
    {
        return m_totalIterations;
    }


    std::uint64_t
    baseIterationsPerTask() const
    {
        return m_baseIterationsPerTask;
    }
};