#pragma once

#include <atomic>
#include <cstddef>

#include "core/CoreLockGuard.hpp"
#include "core/CoreSpinLock.hpp"
#include "core/CoreStartBarrier.hpp"
#include "core/CoreWorkSignal.hpp"

#include "runtime/Task.hpp"
#include "runtime/WorkerState.hpp"


// ---------------------------------------------------------
// CoreSchedulerStatistics
//
// Thread Pool 전체의 Scheduler 통계를
// Benchmark 코드로 전달하기 위한 구조체.
// ---------------------------------------------------------
struct CoreSchedulerStatistics
{
    std::size_t executedTasks = 0;

    std::size_t stolenTasks = 0;

    std::size_t stealAttempts = 0;

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


    // Queue에서 대기 중이거나
    // 실행 중인 전체 미완료 Task
    std::atomic<std::size_t> m_remainingTasks;


    // shutdown 이후 Task 제출 차단
    std::atomic<bool> m_shutdownRequested;


    // Idle Worker Sleep / Wake
    CoreWorkSignal m_workSignal;


    // Benchmark 시작 시점 동기화
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


    void executeTask(
        std::size_t workerId,
        const Task& task
    );


public:
    explicit CoreThreadPool(
        std::size_t workerCount,
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


    void printStatistics() const;


    // Benchmark에서 사용할 전체 통계 반환
    CoreSchedulerStatistics
        getStatistics() const;
};