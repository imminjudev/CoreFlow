#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "benchmark/TaskGranularityWorkload.hpp"
#include "runtime/CoreThreadPool.hpp"


// ---------------------------------------------------------
// SingleRunResult
//
// Benchmark 한 번의 결과.
// ---------------------------------------------------------
struct SingleRunResult
{
    double elapsedMs;

    CoreSchedulerStatistics statistics;

    std::uint64_t checksum;
};


// ---------------------------------------------------------
// GranularityResult
//
// 특정 Task Count + Worker Count 조합의
// 반복 Benchmark 결과.
// ---------------------------------------------------------
struct GranularityResult
{
    std::size_t taskCount;

    std::uint64_t iterationsPerTask;

    std::size_t workerCount;


    double averageMs;

    double minMs;

    double maxMs;


    // -----------------------------------------------------
    // 같은 Worker 수에서
    // 가장 Coarse한 8 Task와 비교한 실행시간 비율.
    //
    // 1.00:
    //     거의 동일
    //
    // 1.10:
    //     약 10% 더 오래 걸림
    // -----------------------------------------------------
    double relativeToCoarse;


    // Coarse 대비 시간 변화율
    double deltaPercent;


    // 동일 Granularity의 1 Worker 대비 Speedup
    double speedup;

    double efficiency;


    double averageSteals;

    double averageAttempts;
};


// ---------------------------------------------------------
// Benchmark 한 번 실행
// ---------------------------------------------------------
SingleRunResult runSingleBenchmark(
    std::size_t workerCount,
    const TaskGranularityWorkload& workload
)
{
    // -----------------------------------------------------
    // 이번 실험에서는 Steal Policy를 Sequential로 고정.
    //
    // v0.19의 독립 변수는 Victim Policy가 아니라
    // Task Granularity이기 때문이다.
    // -----------------------------------------------------
    CoreThreadPool pool(
        workerCount,
        SchedulingMode::WorkStealing,
        StealPolicy::Sequential,
        false
    );


    // Worker 생성 후 Barrier에서 대기
    pool.start();


    const Task* tasks =
        workload.tasks();


    // -----------------------------------------------------
    // 모든 Task를 실행 전에 미리 배치한다.
    // -----------------------------------------------------
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


    // Start Barrier가 닫혀 있으므로
    // 이 시점에서는 모든 Task가 Queue 안에 있어야 한다.
    if (
        pool.queuedTaskCount()
        != workload.taskCount()
    )
    {
        std::cout
            << "[ERROR] Initial queued task mismatch! "
            << "expected="
            << workload.taskCount()
            << " actual="
            << pool.queuedTaskCount()
            << '\n';
    }


    // -----------------------------------------------------
    // Scheduler 실행시간 측정 시작
    // -----------------------------------------------------
    auto start =
        std::chrono::steady_clock::now();


    pool.beginExecution();


    // 추가 Task 제출 없음
    pool.shutdown();


    // 모든 Worker 및 Task 완료
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


    // -----------------------------------------------------
    // Task 유실 / 중복 실행 확인
    // -----------------------------------------------------
    if (
        result.statistics.executedTasks
        != workload.taskCount()
    )
    {
        std::cout
            << "[ERROR] Executed task mismatch! "
            << "expected="
            << workload.taskCount()
            << " actual="
            << result.statistics.executedTasks
            << '\n';
    }


    if (
        pool.queuedTaskCount()
        != 0
    )
    {
        std::cout
            << "[ERROR] Queued tasks remain: "
            << pool.queuedTaskCount()
            << '\n';
    }


    return result;
}


// ---------------------------------------------------------
// 동일한 조건을 여러 번 반복한다.
// ---------------------------------------------------------
GranularityResult runBenchmark(
    std::size_t workerCount,
    const TaskGranularityWorkload& workload,
    std::size_t runCount
)
{
    GranularityResult result{};


    result.taskCount =
        workload.taskCount();


    result.iterationsPerTask =
        workload.baseIterationsPerTask();


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


    std::size_t totalAttempts =
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


    // 나중에 전체 결과를 얻은 뒤 계산
    result.relativeToCoarse =
        0.0;


    result.deltaPercent =
        0.0;


    result.speedup =
        0.0;


    result.efficiency =
        0.0;


    // Compute 결과가 실제 프로그램 흐름에서 사용됨.
    if (checksumSink == 0)
    {
        // Benchmark 판정에는 사용하지 않는다.
    }


    return result;
}


// ---------------------------------------------------------
// Worker 수 하나를 고정하고
// Granularity에 따른 변화를 출력한다.
// ---------------------------------------------------------
void printGranularityTable(
    GranularityResult results[][4],
    std::size_t granularityCount,
    std::size_t workerIndex
)
{
    std::cout
        << "\n=== Workers = "
        << results[0][workerIndex].workerCount
        << " : Granularity Results ===\n\n";


    std::cout
        << "Tasks     "
        << "Iter/Task       "
        << "Avg(ms)       "
        << "vs Coarse   "
        << "Delta(%)    "
        << "Steals    "
        << "Attempts"
        << '\n';


    std::cout
        << "--------------------------------------------------------------------------\n";


    for (
        std::size_t i = 0;
        i < granularityCount;
        i++
    )
    {
        std::cout
            << std::left

            << std::setw(10)
            << results[i][workerIndex].taskCount

            << std::setw(16)
            << results[i][workerIndex]
                   .iterationsPerTask

            << std::setw(14)
            << std::fixed
            << std::setprecision(3)
            << results[i][workerIndex]
                   .averageMs

            << std::setw(12)
            << std::setprecision(3)
            << results[i][workerIndex]
                   .relativeToCoarse

            << std::setw(12)
            << std::setprecision(2)
            << results[i][workerIndex]
                   .deltaPercent

            << std::setw(10)
            << results[i][workerIndex]
                   .averageSteals

            << results[i][workerIndex]
                   .averageAttempts

            << '\n';
    }
}


// ---------------------------------------------------------
// 각 Granularity의 병렬 Scaling 출력
// ---------------------------------------------------------
void printScalingTable(
    GranularityResult results[][4],
    std::size_t granularityCount,
    std::size_t workerCount
)
{
    std::cout
        << "\n=== Parallel Scaling Summary ===\n\n";


    std::cout
        << "Tasks     "
        << "Workers   "
        << "Avg(ms)       "
        << "Speedup   "
        << "Eff(%)"
        << '\n';


    std::cout
        << "---------------------------------------------------------\n";


    for (
        std::size_t g = 0;
        g < granularityCount;
        g++
    )
    {
        for (
            std::size_t w = 0;
            w < workerCount;
            w++
        )
        {
            std::cout
                << std::left

                << std::setw(10)
                << results[g][w].taskCount

                << std::setw(10)
                << results[g][w].workerCount

                << std::setw(14)
                << std::fixed
                << std::setprecision(3)
                << results[g][w].averageMs

                << std::setw(10)
                << std::setprecision(2)
                << results[g][w].speedup

                << results[g][w].efficiency

                << '\n';
        }


        std::cout
            << '\n';
    }
}


// ---------------------------------------------------------
// main
// ---------------------------------------------------------
int main()
{
    std::cout
        << "=== CoreFlow v0.19 Task Granularity Benchmark ===\n\n";


    // -----------------------------------------------------
    // 모든 Benchmark의 총 CPU 연산량은 동일.
    // -----------------------------------------------------
    constexpr std::uint64_t totalIterations =
        1000000000ULL;


    // -----------------------------------------------------
    // Task 개수
    //
    // 전부 8의 배수이므로
    // 1 / 2 / 4 / 8 Worker에 Round-Robin으로
    // 균등하게 분배된다.
    // -----------------------------------------------------
    const std::size_t taskCounts[] =
    {
        8,
        40,
        200,
        1000
    };


    constexpr std::size_t granularityCaseCount =
        sizeof(taskCounts)
        /
        sizeof(taskCounts[0]);


    // -----------------------------------------------------
    // Worker 개수
    // -----------------------------------------------------
    const std::size_t workerCounts[] =
    {
        1,
        2,
        4,
        8
    };


    constexpr std::size_t workerCaseCount =
        sizeof(workerCounts)
        /
        sizeof(workerCounts[0]);


    // 각 조건당 3번 반복
    constexpr std::size_t runCount =
        3;


    // -----------------------------------------------------
    // [Granularity][Workers]
    //
    // 4 × 4 결과 저장
    // -----------------------------------------------------
    GranularityResult results
        [granularityCaseCount]
        [workerCaseCount];


    // -----------------------------------------------------
    // Benchmark 실행
    // -----------------------------------------------------
    for (
        std::size_t g = 0;
        g < granularityCaseCount;
        g++
    )
    {
        TaskGranularityWorkload workload(
            taskCounts[g],
            totalIterations
        );


        std::cout
            << "============================================================\n";


        std::cout
            << "Tasks: "
            << workload.taskCount()
            << '\n';


        std::cout
            << "Iterations / Task: "
            << workload.baseIterationsPerTask()
            << '\n';


        std::cout
            << "Total Iterations: "
            << workload.totalIterations()
            << '\n';


        std::cout
            << "============================================================\n\n";


        for (
            std::size_t w = 0;
            w < workerCaseCount;
            w++
        )
        {
            std::cout
                << "[Workers = "
                << workerCounts[w]
                << "]\n";


            results[g][w] =
                runBenchmark(
                    workerCounts[w],
                    workload,
                    runCount
                );


            std::cout
                << '\n';
        }
    }


    // -----------------------------------------------------
    // 1. Coarse Task와 비교
    //
    // 같은 Worker 수에서
    // 8 Task 실행시간을 기준값으로 사용한다.
    // -----------------------------------------------------
    for (
        std::size_t w = 0;
        w < workerCaseCount;
        w++
    )
    {
        const double coarseBaseline =
            results[0][w].averageMs;


        for (
            std::size_t g = 0;
            g < granularityCaseCount;
            g++
        )
        {
            results[g][w].relativeToCoarse =
                results[g][w].averageMs
                /
                coarseBaseline;


            results[g][w].deltaPercent =
                (
                    (
                        results[g][w].averageMs
                        -
                        coarseBaseline
                    )
                    /
                    coarseBaseline
                )
                * 100.0;
        }
    }


    // -----------------------------------------------------
    // 2. Worker Scaling
    //
    // 각 Granularity의 1 Worker 결과를 기준으로
    // Speedup / Efficiency 계산.
    // -----------------------------------------------------
    for (
        std::size_t g = 0;
        g < granularityCaseCount;
        g++
    )
    {
        const double singleWorkerBaseline =
            results[g][0].averageMs;


        for (
            std::size_t w = 0;
            w < workerCaseCount;
            w++
        )
        {
            results[g][w].speedup =
                singleWorkerBaseline
                /
                results[g][w].averageMs;


            results[g][w].efficiency =
                (
                    results[g][w].speedup
                    /
                    static_cast<double>(
                        results[g][w].workerCount
                    )
                )
                * 100.0;
        }
    }


    // -----------------------------------------------------
    // Granularity 결과
    // -----------------------------------------------------
    std::cout
        << "\n\n============================================\n"
        << "FINAL GRANULARITY RESULTS\n"
        << "============================================\n";


    for (
        std::size_t w = 0;
        w < workerCaseCount;
        w++
    )
    {
        printGranularityTable(
            results,
            granularityCaseCount,
            w
        );
    }


    // -----------------------------------------------------
    // Parallel Scaling 결과
    // -----------------------------------------------------
    printScalingTable(
        results,
        granularityCaseCount,
        workerCaseCount
    );


    return 0;
}