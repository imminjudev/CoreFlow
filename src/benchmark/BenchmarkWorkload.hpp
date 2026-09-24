#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/Task.hpp"


enum class WorkloadType
{
    SleepBalanced,

    CpuBalanced,

    CpuImbalanced
};


// ---------------------------------------------------------
// BenchmarkWorkload
//
// Benchmark에 사용할 Task 배열을 생성한다.
//
// std::vector 대신
// new[] / delete[]를 직접 사용한다.
// ---------------------------------------------------------
class BenchmarkWorkload
{
private:
    Task* m_tasks;

    std::size_t m_taskCount;

    const char* m_name;

    const char* m_workUnit;

    std::uint64_t m_totalWorkAmount;


private:
    // -----------------------------------------------------
    // Sleep Balanced
    //
    // 40 Tasks × 100ms
    //
    // Total = 4000ms
    //
    // CPU를 계속 사용하는 작업이 아니라
    // I/O Waiting과 비슷한 형태를 흉내낸다.
    // -----------------------------------------------------
    void buildSleepBalanced()
    {
        m_name =
            "Sleep Balanced";

        m_workUnit =
            "ms";

        m_taskCount =
            40;

        m_totalWorkAmount =
            0;


        m_tasks =
            new Task[m_taskCount];


        for (
            std::size_t i = 0;
            i < m_taskCount;
            i++
        )
        {
            constexpr std::uint64_t durationMs =
                100;


            m_tasks[i] =
                Task{
                    static_cast<int>(i + 1),
                    "SleepTask",
                    TaskType::Sleep,
                    durationMs
                };


            m_totalWorkAmount +=
                durationMs;
        }
    }


    // -----------------------------------------------------
    // CPU Balanced
    //
    // 모든 Task가 같은 양의 실제 CPU 계산을 수행한다.
    //
    // 40 Tasks
    // 각 25,000,000 iterations
    //
    // Total = 1,000,000,000 iterations
    // -----------------------------------------------------
    void buildCpuBalanced()
    {
        m_name =
            "CPU Balanced";

        m_workUnit =
            "iterations";

        m_taskCount =
            40;

        m_totalWorkAmount =
            0;


        m_tasks =
            new Task[m_taskCount];


        constexpr std::uint64_t iterations =
            25000000ULL;


        for (
            std::size_t i = 0;
            i < m_taskCount;
            i++
        )
        {
            m_tasks[i] =
                Task{
                    static_cast<int>(i + 1),
                    "ComputeTask",
                    TaskType::Compute,
                    iterations
                };


            m_totalWorkAmount +=
                iterations;
        }
    }


    // -----------------------------------------------------
    // CPU Imbalanced
    //
    // 실제 CPU 계산량이 서로 다른 Task를 만든다.
    //
    // 8개마다 Long Task 하나.
    //
    // Long:
    //     80,000,000 iterations
    //
    // Short:
    //      8,000,000 iterations
    //
    // Work Stealing이 CPU-Bound 환경에서도
    // Load Imbalance를 처리하는지 보기 위한 Workload.
    // -----------------------------------------------------
    void buildCpuImbalanced()
    {
        m_name =
            "CPU Imbalanced";

        m_workUnit =
            "iterations";

        m_taskCount =
            40;

        m_totalWorkAmount =
            0;


        m_tasks =
            new Task[m_taskCount];


        for (
            std::size_t i = 0;
            i < m_taskCount;
            i++
        )
        {
            bool isLongTask =
                (i % 8) == 0;


            std::uint64_t iterations =
                isLongTask
                    ? 80000000ULL
                    : 8000000ULL;


            const char* taskName =
                isLongTask
                    ? "LongComputeTask"
                    : "ShortComputeTask";


            m_tasks[i] =
                Task{
                    static_cast<int>(i + 1),
                    taskName,
                    TaskType::Compute,
                    iterations
                };


            m_totalWorkAmount +=
                iterations;
        }
    }


public:
    explicit BenchmarkWorkload(
        WorkloadType type
    )
        : m_tasks(nullptr),
          m_taskCount(0),
          m_name("Unknown"),
          m_workUnit("Unknown"),
          m_totalWorkAmount(0)
    {
        switch (type)
        {
        case WorkloadType::SleepBalanced:

            buildSleepBalanced();

            break;


        case WorkloadType::CpuBalanced:

            buildCpuBalanced();

            break;


        case WorkloadType::CpuImbalanced:

            buildCpuImbalanced();

            break;
        }
    }


    ~BenchmarkWorkload()
    {
        delete[] m_tasks;

        m_tasks =
            nullptr;
    }


    BenchmarkWorkload(
        const BenchmarkWorkload&
    ) = delete;


    BenchmarkWorkload& operator=(
        const BenchmarkWorkload&
    ) = delete;


    const Task* tasks() const
    {
        return m_tasks;
    }


    std::size_t taskCount() const
    {
        return m_taskCount;
    }


    const char* name() const
    {
        return m_name;
    }


    const char* workUnit() const
    {
        return m_workUnit;
    }


    std::uint64_t totalWorkAmount() const
    {
        return m_totalWorkAmount;
    }
};