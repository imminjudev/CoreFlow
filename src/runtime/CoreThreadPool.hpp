#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/CoreLockGuard.hpp"
#include "core/CoreSpinLock.hpp"
#include "core/CoreStartBarrier.hpp"
#include "core/CoreWorkSignal.hpp"

#include "runtime/Task.hpp"
#include "runtime/WorkerState.hpp"


// ---------------------------------------------------------
// SchedulingMode
// ---------------------------------------------------------
enum class SchedulingMode
{
    LocalOnly,

    WorkStealing
};


// ---------------------------------------------------------
// StealPolicy
//
// Sequential:
//
//     현재 Worker 다음 번호부터 순서대로 검사.
//
// RandomStart:
//
//     첫 Victim을 PRNG로 선택한 뒤,
//     그 위치에서부터 원형으로 모든 Victim 검사.
//
// 둘 다 최악의 경우 모든 Victim을 검사한다.
// 탐색 순서만 다르다.
// ---------------------------------------------------------
enum class StealPolicy
{
    Sequential,

    RandomStart
};


// ---------------------------------------------------------
// Thread Pool 전체 통계
// ---------------------------------------------------------
struct CoreSchedulerStatistics
{
    std::size_t executedTasks = 0;

    std::size_t stolenTasks = 0;

    std::size_t stealAttempts = 0;

    std::size_t successfulStealProbes = 0;

    std::size_t failedStealRounds = 0;

    std::size_t sleepCount = 0;

    std::size_t wakeCount = 0;
};


class CoreThreadPool
{
private:
    WorkerState* m_workers;

    std::size_t m_workerCount;

    std::size_t m_nextWorker;

    CoreSpinLock m_submitLock;

    bool m_started;

    bool m_verbose;


    SchedulingMode m_mode;

    StealPolicy m_stealPolicy;


    std::atomic<std::size_t> m_remainingTasks;

    std::atomic<bool> m_shutdownRequested;


    CoreWorkSignal m_workSignal;

    CoreStartBarrier m_startBarrier;


    // CPU Compute 결과 저장
    std::atomic<std::uint64_t> m_computeSink;


private:
    void workerLoop(
        std::size_t workerId
    );


    // 선택된 Steal Policy 실행
    bool trySteal(
        std::size_t thiefId,
        Task& task,
        std::size_t& victimId
    );


    // Sequential Victim Search
    bool tryStealSequential(
        std::size_t thiefId,
        Task& task,
        std::size_t& victimId
    );


    // Random-Start Victim Search
    bool tryStealRandomStart(
        std::size_t thiefId,
        Task& task,
        std::size_t& victimId
    );


    // Worker 전용 PRNG
    std::uint64_t nextRandom(
        std::size_t workerId
    );


    void executeTask(
        std::size_t workerId,
        const Task& task
    );


    std::uint64_t executeComputeKernel(
        const Task& task
    );


public:
    explicit CoreThreadPool(
        std::size_t workerCount,
        SchedulingMode mode =
            SchedulingMode::WorkStealing,
        StealPolicy stealPolicy =
            StealPolicy::Sequential,
        bool verbose = true
    );


    ~CoreThreadPool();


    CoreThreadPool(
        const CoreThreadPool&
    ) = delete;


    CoreThreadPool& operator=(
        const CoreThreadPool&
    ) = delete;


    bool submit(
        const Task& task
    );


    void start();

    void beginExecution();

    void shutdown();

    void wait();


    std::size_t pendingTaskCount();


    CoreSchedulerStatistics
        getStatistics() const;


    void printStatistics() const;


    std::uint64_t
        computeChecksum() const;
};