#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>

#include "benchmark/BenchmarkWorkload.hpp"
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
};


// ---------------------------------------------------------
// BenchmarkResult
//
// 동일한 Worker 수로 여러 번 실행한 결과.
// ---------------------------------------------------------
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
// runSingleBenchmark
//
// 하나의 Workload를 한 번 실행한다.
// ---------------------------------------------------------
SingleRunResult runSingleBenchmark(
    std::size_t workerCount,
    const BenchmarkWorkload& workload
)
{
    // Benchmark에서는 Runtime 로그 OFF
    CoreThreadPool pool(
        workerCount,
        false
    );


    // Worker 생성 후 Start Barrier에서 대기
    pool.start();


    // -----------------------------------------------------
    // 모든 Task를 실행 전에 미리 배치한다.
    // -----------------------------------------------------
    const Task* tasks =
        workload.tasks();


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


    // -----------------------------------------------------
    // Scheduler 실행시간 측정 시작
    // -----------------------------------------------------
    auto start =
        std::chrono::steady_clock::now();


    pool.beginExecution();


    // 모든 Task가 이미 제출됐으므로
    // 추가 Task 제출 차단
    pool.shutdown();


    // 모든 Worker 종료 대기
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


    // Worker 종료 이후이므로
    // 통계를 안전하게 읽을 수 있다.
    result.statistics =
        pool.getStatistics();


    return result;
}


// ---------------------------------------------------------
// runBenchmark
//
// 같은 조건을 여러 번 반복한다.
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


    // 아래 두 값은
    // 1 Worker Baseline이 계산된 뒤 채운다.
    result.speedup =
        0.0;


    result.efficiency =
        0.0;


    return result;
}


// ---------------------------------------------------------
// printBenchmarkTable
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


// ---------------------------------------------------------
// main
// ---------------------------------------------------------
int main()
{
    std::cout
        << "=== CoreFlow v0.14 Workload Benchmark ===\n\n";


    // -----------------------------------------------------
    // 세 종류의 Workload 생성
    // -----------------------------------------------------
    BenchmarkWorkload balanced(
        WorkloadType::Balanced
    );


    BenchmarkWorkload imbalanced(
        WorkloadType::Imbalanced
    );


    BenchmarkWorkload fineGrained(
        WorkloadType::FineGrained
    );


    BenchmarkWorkload* workloads[] =
    {
        &balanced,
        &imbalanced,
        &fineGrained
    };


    constexpr std::size_t workloadCount =
        sizeof(workloads)
        / sizeof(workloads[0]);


    // -----------------------------------------------------
    // 비교할 Worker 개수
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
        / sizeof(workerCounts[0]);


    // Workload 종류가 늘었으므로
    // 우선 각 조건당 3번 반복한다.
    constexpr std::size_t runCount =
        3;


    // -----------------------------------------------------
    // Workload별 Benchmark
    // -----------------------------------------------------
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
            << "Total Simulated Work: "
            << workload.totalDurationMs()
            << " ms\n";


        std::cout
            << "==================================================\n\n";


        BenchmarkResult results[
            workerCaseCount
        ];


        // -------------------------------------------------
        // 1 / 2 / 4 / 8 Worker
        // -------------------------------------------------
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


        // -------------------------------------------------
        // 1 Worker를 Baseline으로 Speedup 계산
        // -------------------------------------------------
        double baselineMs =
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


            // ---------------------------------------------
            // Parallel Efficiency
            //
            // Efficiency =
            //
            //     Speedup
            //     -------
            //     Workers
            //
            // × 100
            //
            // 2 Worker에서 2.0x라면 100%
            // ---------------------------------------------
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


        // 최종 표 출력
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