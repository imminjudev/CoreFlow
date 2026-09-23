#pragma once

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

    // 다음 Task를 어느 Worker에게 줄지 결정
    std::size_t m_nextWorker;

    // submit()이 여러 Thread에서 호출될 경우
    // m_nextWorker 보호용
    CoreSpinLock m_submitLock;

    bool m_started;


private:
    void workerLoop(
        std::size_t workerId
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