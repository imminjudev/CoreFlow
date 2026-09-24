#pragma once

#include <cstddef>

#include "runtime/Task.hpp"


enum class WorkloadType
{
    Balanced,
    Imbalanced,
    FineGrained
};


// ---------------------------------------------------------
// BenchmarkWorkload
//
// Benchmark에서 사용할 Task 집합을 생성하고 관리한다.
//
// std::vector를 사용하지 않고
// Task 배열을 직접 new[] / delete[]로 관리한다.
// ---------------------------------------------------------
class BenchmarkWorkload
{
private:
    Task* m_tasks;

    std::size_t m_taskCount;

    const char* m_name;

    std::size_t m_totalDurationMs;


private:
    // -----------------------------------------------------
    // Balanced
    //
    // 40 Tasks × 100ms
    //
    // Total = 4000ms
    // -----------------------------------------------------
    void buildBalanced()
    {
        m_name =
            "Balanced";

        m_taskCount =
            40;

        m_totalDurationMs =
            0;


        m_tasks =
            new Task[m_taskCount];


        for (
            std::size_t i = 0;
            i < m_taskCount;
            i++
        )
        {
            constexpr int durationMs =
                100;


            m_tasks[i] =
                Task{
                    static_cast<int>(i + 1),
                    "BalancedTask",
                    durationMs
                };


            m_totalDurationMs +=
                durationMs;
        }
    }


    // -----------------------------------------------------
    // Imbalanced
    //
    // 40개의 Task를 만든다.
    //
    // 8개마다 긴 Task 하나:
    //
    // Long  = 600ms
    // Short = 50ms
    //
    // 초기 Round-Robin 배치가 불균형해지도록
    // 일부러 설계한 Workload다.
    //
    // 5 Long Tasks
    // 35 Short Tasks
    //
    // Total:
    //
    // 5 * 600
    // +
    // 35 * 50
    //
    // = 4750ms
    // -----------------------------------------------------
    void buildImbalanced()
    {
        m_name =
            "Imbalanced";

        m_taskCount =
            40;

        m_totalDurationMs =
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


            int durationMs =
                isLongTask
                    ? 600
                    : 50;


            const char* taskName =
                isLongTask
                    ? "LongTask"
                    : "ShortTask";


            m_tasks[i] =
                Task{
                    static_cast<int>(i + 1),
                    taskName,
                    durationMs
                };


            m_totalDurationMs +=
                durationMs;
        }
    }


    // -----------------------------------------------------
    // Fine-Grained
    //
    // 매우 작은 Task를 많이 만든다.
    //
    // 100 Tasks × 20ms
    //
    // Total = 2000ms
    //
    // Task 개수가 많을 때
    // Scheduler / Work Stealing 동작을 확인한다.
    // -----------------------------------------------------
    void buildFineGrained()
    {
        m_name =
            "Fine-Grained";

        m_taskCount =
            100;

        m_totalDurationMs =
            0;


        m_tasks =
            new Task[m_taskCount];


        for (
            std::size_t i = 0;
            i < m_taskCount;
            i++
        )
        {
            constexpr int durationMs =
                20;


            m_tasks[i] =
                Task{
                    static_cast<int>(i + 1),
                    "FineTask",
                    durationMs
                };


            m_totalDurationMs +=
                durationMs;
        }
    }


public:
    explicit BenchmarkWorkload(
        WorkloadType type
    )
        : m_tasks(nullptr),
          m_taskCount(0),
          m_name("Unknown"),
          m_totalDurationMs(0)
    {
        switch (type)
        {
        case WorkloadType::Balanced:
            buildBalanced();
            break;


        case WorkloadType::Imbalanced:
            buildImbalanced();
            break;


        case WorkloadType::FineGrained:
            buildFineGrained();
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


    std::size_t totalDurationMs() const
    {
        return m_totalDurationMs;
    }
};