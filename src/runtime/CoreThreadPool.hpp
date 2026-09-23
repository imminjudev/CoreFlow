#pragma once

#include <atomic>
#include <cstddef>

#include "core/CoreLockGuard.hpp"
#include "core/CoreSpinLock.hpp"
#include "runtime/Task.hpp"
#include "runtime/WorkerState.hpp"


class CoreThreadPool
{
private:
    WorkerState* m_workers;

    std::size_t m_workerCount;

    // Round-Robin으로 다음 Task를 받을 Worker
    std::size_t m_nextWorker;

    CoreSpinLock m_submitLock;

    bool m_started;

    // 제출됐지만 아직 실행 완료되지 않은 Task 수
    std::atomic<std::size_t> m_remainingTasks;

    // shutdown() 호출 여부
    std::atomic<bool> m_shutdownRequested;


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
};