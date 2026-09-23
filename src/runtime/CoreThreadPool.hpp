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

    // Round-Robin 배치용
    std::size_t m_nextWorker;

    CoreSpinLock m_submitLock;

    bool m_started;


    // -----------------------------------------------------
    // Queue에 있거나 현재 실행 중인
    // 전체 미완료 Task 개수
    // -----------------------------------------------------
    std::atomic<std::size_t> m_remainingTasks;


    // shutdown 요청 여부
    std::atomic<bool> m_shutdownRequested;


    // -----------------------------------------------------
    // Worker 전체가 공유하는 Wake/Sleep Signal
    // -----------------------------------------------------
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
};