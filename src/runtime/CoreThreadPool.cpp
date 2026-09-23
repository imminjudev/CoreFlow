#include "runtime/CoreThreadPool.hpp"

#include <chrono>
#include <iostream>


CoreThreadPool::CoreThreadPool(
    std::size_t workerCount
)
    : m_workers(nullptr),
      m_workerCount(workerCount),
      m_nextWorker(0),
      m_started(false)
{
    if (m_workerCount > 0)
    {
        m_workers =
            new WorkerState[m_workerCount];

        for (
            std::size_t i = 0;
            i < m_workerCount;
            i++
        )
        {
            m_workers[i].id = i;
        }
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
// submit
//
// Round-Robin 방식으로 Task를 Worker에게 분배한다.
//
// Worker 2개라면:
//
// T1 -> W0
// T2 -> W1
// T3 -> W0
// T4 -> W1
// T5 -> W0
// ---------------------------------------------------------
bool CoreThreadPool::submit(
    const Task& task
)
{
    if (m_workerCount == 0)
    {
        return false;
    }


    std::size_t targetWorker;


    {
        CoreLockGuard<CoreSpinLock> guard(
            m_submitLock
        );

        targetWorker =
            m_nextWorker;

        m_nextWorker =
            (m_nextWorker + 1)
            % m_workerCount;
    }


    bool success =
        m_workers[targetWorker]
            .localQueue
            .pushBack(task);


    if (success)
    {
        std::cout
            << "[SCHEDULER] Task "
            << task.id
            << " -> Worker "
            << targetWorker
            << '\n';
    }


    return success;
}


// ---------------------------------------------------------
// Worker Thread 시작
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
        m_workers[i].thread =
            std::thread(
                &CoreThreadPool::workerLoop,
                this,
                i
            );
    }
}


// ---------------------------------------------------------
// 모든 Local Queue를 close.
//
// Queue에 남아있는 Task는 끝까지 처리한다.
// ---------------------------------------------------------
void CoreThreadPool::shutdown()
{
    for (
        std::size_t i = 0;
        i < m_workerCount;
        i++
    )
    {
        m_workers[i]
            .localQueue
            .close();
    }
}


// ---------------------------------------------------------
// Worker 종료 대기
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
        if (
            m_workers[i]
                .thread
                .joinable()
        )
        {
            m_workers[i]
                .thread
                .join();
        }
    }


    m_started = false;
}


// ---------------------------------------------------------
// Worker Loop
//
// Worker는 자기 Local Queue만 본다.
//
// 현재 v0.8에는 Work Stealing이 없다.
//
// 자신의 Queue가 비어 있으면 기다린다.
//
// shutdown 후 자신의 Queue까지 완전히 비면 종료한다.
// ---------------------------------------------------------
void CoreThreadPool::workerLoop(
    std::size_t workerId
)
{
    std::cout
        << "[Worker "
        << workerId
        << "] started\n";


    WorkerState& worker =
        m_workers[workerId];


    while (true)
    {
        Task task{};


        if (
            !worker
                .localQueue
                .waitPopBack(task)
        )
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


// ---------------------------------------------------------
// Task 실행
// ---------------------------------------------------------
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


// ---------------------------------------------------------
// 모든 Local Queue에 남은 Task 수 합산
// ---------------------------------------------------------
std::size_t
CoreThreadPool::pendingTaskCount()
{
    std::size_t total = 0;


    for (
        std::size_t i = 0;
        i < m_workerCount;
        i++
    )
    {
        total +=
            m_workers[i]
                .localQueue
                .size();
    }


    return total;
}