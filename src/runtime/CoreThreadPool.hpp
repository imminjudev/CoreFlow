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


enum class SchedulingMode
{
    LocalOnly,

    WorkStealing
};


enum class StealPolicy
{
    Sequential,

    RandomStart
};


struct CoreSchedulerStatistics
{
    std::size_t executedTasks = 0;

    std::size_t stolenTasks = 0;

    std::size_t stealAttempts = 0;

    std::size_t successfulStealProbes = 0;

    std::size_t failedStealRounds = 0;

    std::size_t stealSkippedNoQueuedWork = 0;

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


    // Queue 대기 + 실행 중인 전체 Task
    std::atomic<std::size_t> m_remainingTasks;


    // Queue 안에 실제 존재하는 Task
    std::atomic<std::size_t> m_queuedTasks;


    std::atomic<bool> m_shutdownRequested;


    CoreWorkSignal m_workSignal;

    CoreStartBarrier m_startBarrier;


private:
    void workerLoop(
        std::size_t workerId
    );


    bool trySteal(
        std::size_t thiefId,
        Task& task,
        std::size_t& victimId
    );


    bool tryStealSequential(
        std::size_t thiefId,
        Task& task,
        std::size_t& victimId
    );


    bool tryStealRandomStart(
        std::size_t thiefId,
        Task& task,
        std::size_t& victimId
    );


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


    std::size_t queuedTaskCount() const;


    CoreSchedulerStatistics
        getStatistics() const;


    void printStatistics() const;


    // -----------------------------------------------------
    // 모든 Worker의 Compute Checksum을 합산한다.
    //
    // pool.wait() 이후 호출하는 것을 전제로 한다.
    // -----------------------------------------------------
    std::uint64_t
        computeChecksum() const;
};