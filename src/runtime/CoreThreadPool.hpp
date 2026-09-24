#pragma once

#include <atomic>
#include <cstddef>

#include "core/CoreLockGuard.hpp"
#include "core/CoreSpinLock.hpp"
#include "core/CoreWorkSignal.hpp"

#include "runtime/Task.hpp"
#include "runtime/WorkerState.hpp"


class CoreThreadPool
{
private:
    WorkerState* m_workers;

    std::size_t m_workerCount;

    std::size_t m_nextWorker;

    CoreSpinLock m_submitLock;

    bool m_started;


    // Queue 대기 + 실행 중인 모든 미완료 Task
    std::atomic<std::size_t> m_remainingTasks;


    // shutdown 이후 새로운 submit을 차단한다.
    std::atomic<bool> m_shutdownRequested;


    // Sleeping Worker들을 깨우기 위한 Global Signal
    CoreWorkSignal m_workSignal;


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
        std::size_t workerCount
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

    void shutdown();

    void wait();


    std::size_t pendingTaskCount();


    // 모든 Worker가 종료된 뒤
    // Scheduler 실행 통계를 출력한다.
    void printStatistics() const;
};