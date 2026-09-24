#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "benchmark/BenchmarkWorkload.hpp"
#include "runtime/CoreThreadPool.hpp"


struct SingleRunResult
{
    double elapsedMs;

    CoreSchedulerStatistics statistics;

    std::uint64_t checksum;
};


struct BenchmarkResult
{
    std::size_t workerCount;

    double averageMs;

    double minMs;

    double maxMs;

    double speedup;

    double efficiency;

    double averageSteals;

    double averageStealAttempts;
};


// ---------------------------------------------------------
// Benchmark 한 번 실행
// ---------------------------------------------------------
SingleRunResult runSingleBenchmark(
    std::size_t workerCount,
    const BenchmarkWorkload& workload
)
{
    CoreThreadPool pool(
        workerCount,
        false
    );


    // Worker 생성 후 Barrier에서 대기
    pool.start();


    const Task* tasks =
        workload.tasks();


    // Worker 실행 전에 모든 Task 배치
    for (
        std::size_t i = 0;
        i < workload.taskCount();
        i++
    )
    {
        pool.submit(
            tasks[i]
        );
    }


    // 실제 Scheduler 실행 구간
    auto start =
        std::chrono::steady_clock::now();


    pool.beginExecution();


    pool.shutdown();


    pool.wait();


    auto end =
        std::chrono::steady_clock::now();


    auto elapsed =
        std::chrono::duration_cast<
            std::chrono::microseconds
        >(
            end - start
        );


    SingleRunResult result{};


    result.elapsedMs =
        static_cast<double>(
            elapsed.count()
        )
        / 1000.0;


    result.statistics =
        pool.getStatistics();


    result.checksum =
        pool.computeChecksum();


    return result;
}


// ---------------------------------------------------------
// 같은 조건 여러 번 반복
// ---------------------------------------------------------
BenchmarkResult runBenchmark(
    std::size_t workerCount,
    const BenchmarkWorkload& workload,
    std::size_t runCount
)
{
    BenchmarkResult result{};


    result.workerCount =
        workerCount;


    double totalMs =
        0.0;


    double minMs =
        0.0;


    double maxMs =
        0.0;


    std::size_t totalSteals =
        0;


    std::size_t totalStealAttempts =
        0;


    std::uint64_t combinedChecksum =
        0;


    for (
        std::size_t run = 0;
        run < runCount;
        run++
    )
    {
        SingleRunResult runResult =
            runSingleBenchmark(
                workerCount,
                workload
            );


        combinedChecksum ^=
            runResult.checksum;


        std::cout
            << "  Run "
            << (run + 1)
            << "/"
            << runCount
            << " : "

            << std::fixed
            << std::setprecision(3)
            << runResult.elapsedMs
            << " ms"

            << " | steals = "
            << runResult
                   .statistics
                   .stolenTasks

            << " | attempts = "
            << runResult
                   .statistics
                   .stealAttempts

            << '\n';


        totalMs +=
            runResult.elapsedMs;


        totalSteals +=
            runResult
                .statistics
                .stolenTasks;


        totalStealAttempts +=
            runResult
                .statistics
                .stealAttempts;


        if (run == 0)
        {
            minMs =
                runResult.elapsedMs;


            maxMs =
                runResult.elapsedMs;
        }
        else
        {
            if (
                runResult.elapsedMs
                < minMs
            )
            {
                minMs =
                    runResult.elapsedMs;
            }


            if (
                runResult.elapsedMs
                > maxMs
            )
            {
                maxMs =
                    runResult.elapsedMs;
            }
        }
    }


    result.averageMs =
        totalMs
        / static_cast<double>(
            runCount
        );


    result.minMs =
        minMs;


    result.maxMs =
        maxMs;


    result.averageSteals =
        static_cast<double>(
            totalSteals
        )
        / static_cast<double>(
            runCount
        );


    result.averageStealAttempts =
        static_cast<double>(
            totalStealAttempts
        )
        / static_cast<double>(
            runCount
        );


    result.speedup =
        0.0;


    result.efficiency =
        0.0;


    // CPU 계산 결과가 실제 프로그램에서
    // 사용되고 있음을 확인하기 위한 값.
    //
    // Benchmark 성능 분석에는 사용하지 않는다.
    if (combinedChecksum != 0)
    {
        // 의도적으로 아무 출력도 하지 않는다.
        // 값 자체가 프로그램 흐름에 사용되고 있다.
    }


    return result;
}


// ---------------------------------------------------------
// 결과 표
// ---------------------------------------------------------
void printBenchmarkTable(
    BenchmarkResult* results,
    std::size_t resultCount
)
{
    std::cout
        << '\n'
        << "Workers   "
        << "Avg(ms)       "
        << "Min(ms)       "
        << "Max(ms)       "
        << "Speedup   "
        << "Eff(%)    "
        << "Steals    "
        << "Attempts"
        << '\n';


    std::cout
        << "--------------------------------------------------------------------------\n";


    for (
        std::size_t i = 0;
        i < resultCount;
        i++
    )
    {
        std::cout
            << std::left

            << std::setw(10)
            << results[i].workerCount

            << std::setw(14)
            << std::fixed
            << std::setprecision(3)
            << results[i].averageMs

            << std::setw(14)
            << results[i].minMs

            << std::setw(14)
            << results[i].maxMs

            << std::setw(10)
            << std::setprecision(2)
            << results[i].speedup

            << std::setw(10)
            << results[i].efficiency

            << std::setw(10)
            << results[i].averageSteals

            << results[i].averageStealAttempts

            << '\n';
    }
}


int main()
{
    std::cout
        << "=== CoreFlow v0.15 CPU Benchmark ===\n\n";


    BenchmarkWorkload sleepBalanced(
        WorkloadType::SleepBalanced
    );


    BenchmarkWorkload cpuBalanced(
        WorkloadType::CpuBalanced
    );


    BenchmarkWorkload cpuImbalanced(
        WorkloadType::CpuImbalanced
    );


    BenchmarkWorkload* workloads[] =
    {
        &sleepBalanced,
        &cpuBalanced,
        &cpuImbalanced
    };


    constexpr std::size_t workloadCount =
        sizeof(workloads)
        / sizeof(workloads[0]);


    const std::size_t workerCounts[] =
    {
        1,
        2,
        4,
        8
    };


    constexpr std::size_t workerCaseCount =
        sizeof(workerCounts)
        / sizeof(workerCounts[0]);


    constexpr std::size_t runCount =
        3;


    for (
        std::size_t workloadIndex = 0;
        workloadIndex < workloadCount;
        workloadIndex++
    )
    {
        BenchmarkWorkload& workload =
            *workloads[workloadIndex];


        std::cout
            << "==================================================\n";


        std::cout
            << "Workload: "
            << workload.name()
            << '\n';


        std::cout
            << "Tasks: "
            << workload.taskCount()
            << '\n';


        std::cout
            << "Total Work: "
            << workload.totalWorkAmount()
            << ' '
            << workload.workUnit()
            << '\n';


        std::cout
            << "==================================================\n\n";


        BenchmarkResult results[
            workerCaseCount
        ];


        for (
            std::size_t i = 0;
            i < workerCaseCount;
            i++
        )
        {
            std::cout
                << "[Benchmark] Workers = "
                << workerCounts[i]
                << '\n';


            results[i] =
                runBenchmark(
                    workerCounts[i],
                    workload,
                    runCount
                );


            std::cout
                << '\n';
        }


        // 1 Worker를 Baseline으로 사용
        const double baselineMs =
            results[0].averageMs;


        for (
            std::size_t i = 0;
            i < workerCaseCount;
            i++
        )
        {
            results[i].speedup =
                baselineMs
                / results[i].averageMs;


            results[i].efficiency =
                (
                    results[i].speedup
                    /
                    static_cast<double>(
                        results[i].workerCount
                    )
                )
                * 100.0;
        }


        std::cout
            << "=== "
            << workload.name()
            << " Results ===\n";


        printBenchmarkTable(
            results,
            workerCaseCount
        );


        std::cout
            << "\n\n";
    }


    return 0;
}