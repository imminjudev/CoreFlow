#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "benchmark/BenchmarkWorkload.hpp"
#include "runtime/CoreThreadPool.hpp"


// ---------------------------------------------------------
// SingleRunResult
// ---------------------------------------------------------
struct SingleRunResult
{
    double elapsedMs;

    CoreSchedulerStatistics statistics;

    std::uint64_t checksum;
};


// ---------------------------------------------------------
// BenchmarkResult
// ---------------------------------------------------------
struct BenchmarkResult
{
    std::size_t workerCount;

    StealPolicy policy;


    double averageMs;

    double minMs;

    double maxMs;


    double averageSteals;

    double averageAttempts;

    double averageSuccessfulProbes;

    double averageFailedRounds;


    // Steal 1회 성공에 필요한
    // 평균 Victim Probe 수
    double successfulSearchDepth;


    // 전체 Queue Probe 중
    // 실제 Steal 성공 비율
    double stealSuccessRate;
};


// ---------------------------------------------------------
// Steal Policy 이름
// ---------------------------------------------------------
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
// Benchmark 한 번 실행
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


    if (
        result.statistics.executedTasks
        != workload.taskCount()
    )
    {
        std::cout
            << "[ERROR] Task count mismatch! "
            << "expected="
            << workload.taskCount()
            << " actual="
            << result.statistics.executedTasks
            << '\n';
    }


    return result;
}


// ---------------------------------------------------------
// 동일 조건 반복 실행
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

            << " | search depth = "
            << std::setprecision(2)
            << searchDepth

            << " | failed rounds = "
            << runResult
                   .statistics
                   .failedStealRounds

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


    result.averageSuccessfulProbes =
        static_cast<double>(
            totalSuccessfulProbes
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
        // CPU 계산 결과는 atomic sink에도 저장되어 있으므로
        // Benchmark correctness 판단에는 사용하지 않는다.
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
        << "FailRounds   "
        << "Success(%)"
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

            << std::setw(13)
            << results[i].averageFailedRounds

            << results[i].stealSuccessRate

            << '\n';
    }
}


// ---------------------------------------------------------
// 직접 비교
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
        << "Random Gain   "
        << "Seq Depth   "
        << "Random Depth"
        << '\n';


    std::cout
        << "-----------------------------------------------------------------------\n";


    for (
        std::size_t i = 0;
        i < resultCount;
        i++
    )
    {
        double randomGain =
            sequential[i].averageMs
            /
            randomStart[i].averageMs;


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

            << std::setw(14)
            << std::setprecision(3)
            << randomGain

            << std::setw(12)
            << std::setprecision(2)
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
        << "=== CoreFlow v0.17 Victim Selection Benchmark ===\n\n";


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


    // 1 Worker에서는 Victim Selection 자체가 없으므로 제외.
    const std::size_t workerCounts[] =
    {
        2,
        4,
        8
    };


    constexpr std::size_t workerCaseCount =
        sizeof(workerCounts)
        / sizeof(workerCounts[0]);


    // 비교 실험이므로 기존보다 5회 반복
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
    // Random Start
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
        << "=== Victim Selection Results ===\n";


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