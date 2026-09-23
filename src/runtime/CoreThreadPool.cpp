#include "runtime/CoreThreadPool.hpp"

#include <chrono>
#include <iostream>


CoreThreadPool::CoreThreadPool(
    std::size_t workerCount
)
    : m_workers(nullptr),
      m_workerCount(workerCount),
      m_started(false)
{
    if (m_workerCount > 0)
    {
        m_workers =
            new std::thread[m_workerCount];
    }
}


CoreThreadPool::~CoreThreadPool()
{
    if (m_started)
    {
        shutdown();
        wait();
    }

    delete[] m_workers;

    m_workers = nullptr;
}


// ---------------------------------------------------------
// Task 제출
//
// 이제 start() 이후에도 Task를 넣을 수 있다.
// ---------------------------------------------------------
bool CoreThreadPool::submit(
    const Task& task
)
{
    return m_taskQueue.push(task);
}


// ---------------------------------------------------------
// Worker 시작
// ---------------------------------------------------------
void CoreThreadPool::start()
{
    if (m_started)
    {
        return;
    }

    m_started = true;

    for (
        std::size_t i = 0;
        i < m_workerCount;
        i++
    )
    {
        m_workers[i] =
            std::thread(
                &CoreThreadPool::workerLoop,
                this,
                i
            );
    }
}


// ---------------------------------------------------------
// shutdown
//
// 더 이상 Task를 받지 않는다.
//
// Queue에 남아 있는 기존 Task는 Worker들이
// 끝까지 처리한다.
//
// 모든 Task가 처리되면 Worker가 종료된다.
// ---------------------------------------------------------
void CoreThreadPool::shutdown()
{
    m_taskQueue.close();
}


// ---------------------------------------------------------
// 모든 Worker 종료 대기
// ---------------------------------------------------------
void CoreThreadPool::wait()
{
    if (!m_started)
    {
        return;
    }

    for (
        std::size_t i = 0;
        i < m_workerCount;
        i++
    )
    {
        if (m_workers[i].joinable())
        {
            m_workers[i].join();
        }
    }

    m_started = false;
}


// ---------------------------------------------------------
// Worker Loop
//
// Queue가 비었다고 종료하지 않는다.
//
// waitPop() 내부에서 잠들었다가
// 새로운 Task가 들어오면 깨어난다.
//
// Queue가 close되고
// 남은 Task도 없을 때만 false를 반환한다.
// ---------------------------------------------------------
void CoreThreadPool::workerLoop(
    std::size_t workerId
)
{
    std::cout
        << "[Worker "
        << workerId
        << "] started\n";

    while (true)
    {
        Task task{};

        if (!m_taskQueue.waitPop(task))
        {
            break;
        }

        executeTask(
            workerId,
            task
        );
    }

    std::cout
        << "[Worker "
        << workerId
        << "] stopped\n";
}


void CoreThreadPool::executeTask(
    std::size_t workerId,
    const Task& task
)
{
    std::cout
        << "[Worker "
        << workerId
        << "] START Task "
        << task.id
        << " | "
        << task.name
        << '\n';

    std::this_thread::sleep_for(
        std::chrono::milliseconds(
            task.durationMs
        )
    );

    std::cout
        << "[Worker "
        << workerId
        << "] END   Task "
        << task.id
        << " | "
        << task.name
        << '\n';
}


std::size_t
CoreThreadPool::pendingTaskCount()
{
    return m_taskQueue.size();
}