#pragma once

#include <cstddef>
#include <thread>

#include "core/CoreBlockingQueue.hpp"
#include "runtime/Task.hpp"


class CoreThreadPool
{
private:
    std::thread* m_workers;

    std::size_t m_workerCount;

    CoreBlockingQueue<Task> m_taskQueue;

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