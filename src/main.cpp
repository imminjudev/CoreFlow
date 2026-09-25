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

    StealPolicy policy;


    double averageMs;

    double minMs;

    double maxMs;


    double averageSteals;

    double averageAttempts;

    double averageFailedRounds;

    double averageSkippedScans;

    double successfulSearchDepth;

    double stealSuccessRate;
};


const char* policyName(
    StealPolicy policy
)
{
    if (
        policy
        == StealPolicy::Sequential
    )
    {
        return "Sequential";
    }


    return "RandomStart";
}


// ---------------------------------------------------------
// Benchmark 1회
// ---------------------------------------------------------
SingleRunResult runSingleBenchmark(
    std::size_t workerCount,
    StealPolicy policy,
    const BenchmarkWorkload& workload
)
{
    CoreThreadPool pool(
        workerCount,
        SchedulingMode::WorkStealing,
        policy,
        false
    );


    pool.start();


    const Task* tasks =
        workload.tasks();


    for (
        std::size_t i = 0;
        i < workload.taskCount();
        i++
    )
    {
        bool submitted =
            pool.submit(
                tasks[i]
            );


        if (!submitted)
        {
            std::cout
                << "[ERROR] Task submission failed\n";
        }
    }


    // Barrier가 닫혀 있으므로
    // 아직 Worker가 Task를 꺼낼 수 없다.
    //
    // 따라서 여기서는 queuedTasks == taskCount여야 한다.
    if (
        pool.queuedTaskCount()
        != workload.taskCount()
    )
    {
        std::cout
            << "[ERROR] Initial queued task count mismatch! "
            << "expected="
            << workload.taskCount()
            << " actual="
            << pool.queuedTaskCount()
            << '\n';
    }


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


    // 모든 Task가 정확히 실행됐는지 검사
    if (
        result.statistics.executedTasks
        != workload.taskCount()
    )
    {
        std::cout
            << "[ERROR] Executed Task count mismatch! "
            << "expected="
            << workload.taskCount()
            << " actual="
            << result.statistics.executedTasks
            << '\n';
    }


    // 실행 종료 후 Queue Task는 0이어야 한다.
    if (
        pool.queuedTaskCount()
        != 0
    )
    {
        std::cout
            << "[ERROR] Queued tasks remain! "
            << pool.queuedTaskCount()
            << '\n';
    }


    return result;
}


// ---------------------------------------------------------
// 반복 Benchmark
// ---------------------------------------------------------
BenchmarkResult runBenchmark(
    std::size_t workerCount,
    StealPolicy policy,
    const BenchmarkWorkload& workload,
    std::size_t runCount
)
{
    BenchmarkResult result{};


    result.workerCount =
        workerCount;


    result.policy =
        policy;


    double totalMs =
        0.0;


    double minMs =
        0.0;


    double maxMs =
        0.0;


    std::size_t totalSteals =
        0;


    std::size_t totalAttempts =
        0;


    std::size_t totalSuccessfulProbes =
        0;


    std::size_t totalFailedRounds =
        0;


    std::size_t totalSkippedScans =
        0;


    std::uint64_t checksumSink =
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
                policy,
                workload
            );


        checksumSink ^=
            runResult.checksum;


        totalMs +=
            runResult.elapsedMs;


        totalSteals +=
            runResult
                .statistics
                .stolenTasks;


        totalAttempts +=
            runResult
                .statistics
                .stealAttempts;


        totalSuccessfulProbes +=
            runResult
                .statistics
                .successfulStealProbes;


        totalFailedRounds +=
            runResult
                .statistics
                .failedStealRounds;


        totalSkippedScans +=
            runResult
                .statistics
                .stealSkippedNoQueuedWork;


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


        double searchDepth =
            0.0;


        if (
            runResult
                .statistics
                .stolenTasks
            > 0
        )
        {
            searchDepth =
                static_cast<double>(
                    runResult
                        .statistics
                        .successfulStealProbes
                )
                /
                static_cast<double>(
                    runResult
                        .statistics
                        .stolenTasks
                );
        }


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

            << " | depth = "
            << std::setprecision(2)
            << searchDepth

            << " | failed = "
            << runResult
                   .statistics
                   .failedStealRounds

            << " | skipped = "
            << runResult
                   .statistics
                   .stealSkippedNoQueuedWork

            << '\n';
    }


    result.averageMs =
        totalMs
        /
        static_cast<double>(
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
        /
        static_cast<double>(
            runCount
        );


    result.averageAttempts =
        static_cast<double>(
            totalAttempts
        )
        /
        static_cast<double>(
            runCount
        );


    result.averageFailedRounds =
        static_cast<double>(
            totalFailedRounds
        )
        /
        static_cast<double>(
            runCount
        );


    result.averageSkippedScans =
        static_cast<double>(
            totalSkippedScans
        )
        /
        static_cast<double>(
            runCount
        );


    if (totalSteals > 0)
    {
        result.successfulSearchDepth =
            static_cast<double>(
                totalSuccessfulProbes
            )
            /
            static_cast<double>(
                totalSteals
            );
    }
    else
    {
        result.successfulSearchDepth =
            0.0;
    }


    if (totalAttempts > 0)
    {
        result.stealSuccessRate =
            (
                static_cast<double>(
                    totalSteals
                )
                /
                static_cast<double>(
                    totalAttempts
                )
            )
            * 100.0;
    }
    else
    {
        result.stealSuccessRate =
            0.0;
    }


    if (checksumSink == 0)
    {
        // Benchmark correctness 판단에는 사용하지 않음.
    }


    return result;
}


// ---------------------------------------------------------
// 결과 표
// ---------------------------------------------------------
void printResultTable(
    BenchmarkResult* results,
    std::size_t resultCount
)
{
    std::cout
        << '\n'
        << "Policy        "
        << "Workers   "
        << "Avg(ms)       "
        << "Steals    "
        << "Attempts   "
        << "Depth     "
        << "Failed    "
        << "Skipped   "
        << "Success(%)"
        << '\n';


    std::cout
        << "----------------------------------------------------------------------------------------\n";


    for (
        std::size_t i = 0;
        i < resultCount;
        i++
    )
    {
        std::cout
            << std::left

            << std::setw(14)
            << policyName(
                results[i].policy
            )

            << std::setw(10)
            << results[i].workerCount

            << std::setw(14)
            << std::fixed
            << std::setprecision(3)
            << results[i].averageMs

            << std::setw(10)
            << std::setprecision(2)
            << results[i].averageSteals

            << std::setw(11)
            << results[i].averageAttempts

            << std::setw(10)
            << results[i].successfulSearchDepth

            << std::setw(10)
            << results[i].averageFailedRounds

            << std::setw(10)
            << results[i].averageSkippedScans

            << results[i].stealSuccessRate

            << '\n';
    }
}


// ---------------------------------------------------------
// Sequential vs RandomStart 비교
// ---------------------------------------------------------
void printComparison(
    BenchmarkResult* sequential,
    BenchmarkResult* randomStart,
    std::size_t resultCount
)
{
    std::cout
        << "\n=== Sequential vs RandomStart ===\n\n";


    std::cout
        << "Workers   "
        << "Seq(ms)       "
        << "Random(ms)    "
        << "Seq Attempts   "
        << "Rnd Attempts   "
        << "Seq Depth   "
        << "Rnd Depth"
        << '\n';


    std::cout
        << "--------------------------------------------------------------------------------\n";


    for (
        std::size_t i = 0;
        i < resultCount;
        i++
    )
    {
        std::cout
            << std::left

            << std::setw(10)
            << sequential[i].workerCount

            << std::setw(14)
            << std::fixed
            << std::setprecision(3)
            << sequential[i].averageMs

            << std::setw(14)
            << randomStart[i].averageMs

            << std::setw(15)
            << std::setprecision(2)
            << sequential[i].averageAttempts

            << std::setw(15)
            << randomStart[i].averageAttempts

            << std::setw(12)
            << sequential[i].successfulSearchDepth

            << randomStart[i].successfulSearchDepth

            << '\n';
    }
}


// ---------------------------------------------------------
// main
// ---------------------------------------------------------
int main()
{
    std::cout
        << "=== CoreFlow v0.18 Queued Task Awareness ===\n\n";


    BenchmarkWorkload workload(
        WorkloadType::CpuImbalanced
    );


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
        << "\n\n";


    const std::size_t workerCounts[] =
    {
        2,
        4,
        8
    };


    constexpr std::size_t workerCaseCount =
        sizeof(workerCounts)
        / sizeof(workerCounts[0]);


    constexpr std::size_t runCount =
        5;


    BenchmarkResult sequentialResults[
        workerCaseCount
    ];


    BenchmarkResult randomResults[
        workerCaseCount
    ];


    // -----------------------------------------------------
    // Sequential
    // -----------------------------------------------------
    std::cout
        << "===== Sequential Victim Search =====\n\n";


    for (
        std::size_t i = 0;
        i < workerCaseCount;
        i++
    )
    {
        std::cout
            << "[Workers = "
            << workerCounts[i]
            << "]\n";


        sequentialResults[i] =
            runBenchmark(
                workerCounts[i],
                StealPolicy::Sequential,
                workload,
                runCount
            );


        std::cout
            << '\n';
    }


    // -----------------------------------------------------
    // RandomStart
    // -----------------------------------------------------
    std::cout
        << "===== Random-Start Victim Search =====\n\n";


    for (
        std::size_t i = 0;
        i < workerCaseCount;
        i++
    )
    {
        std::cout
            << "[Workers = "
            << workerCounts[i]
            << "]\n";


        randomResults[i] =
            runBenchmark(
                workerCounts[i],
                StealPolicy::RandomStart,
                workload,
                runCount
            );


        std::cout
            << '\n';
    }


    std::cout
        << "=== Queued Awareness Results ===\n";


    printResultTable(
        sequentialResults,
        workerCaseCount
    );


    printResultTable(
        randomResults,
        workerCaseCount
    );


    printComparison(
        sequentialResults,
        randomResults,
        workerCaseCount
    );


    return 0;
}