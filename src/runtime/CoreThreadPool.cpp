#include "runtime/CoreThreadPool.hpp"

#include <chrono>
#include <iostream>


// ---------------------------------------------------------
// Constructor
//
// Worker 개수만 받아둔다.
//
// 아직 Thread를 실행하지는 않는다.
//
// new std::thread[workerCount]를 통해
// Worker를 저장할 배열만 준비한다.
// ---------------------------------------------------------
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


// ---------------------------------------------------------
// Destructor
//
// Thread가 아직 join 가능한 상태라면
// 프로그램 종료 전에 반드시 기다린다.
//
// 그 뒤 우리가 new[]로 만든 Worker 배열을
// delete[]로 해제한다.
// ---------------------------------------------------------
CoreThreadPool::~CoreThreadPool()
{
    if (m_started)
    {
        wait();
    }

    delete[] m_workers;

    m_workers = nullptr;
}


// ---------------------------------------------------------
// submit
//
// Task를 Thread-Safe Queue 뒤에 추가한다.
//
// v0.5에서는 반드시 start() 전에 Task를 넣는다고
// 가정한다.
//
// 다음 버전에서는 실행 중에도 submit할 수 있게 한다.
// ---------------------------------------------------------
void CoreThreadPool::submit(
    const Task& task
)
{
    m_taskQueue.push(task);
}


// ---------------------------------------------------------
// start
//
// Worker Thread들을 실제로 생성한다.
//
// 예:
//
// Worker 0 -> workerLoop(0)
// Worker 1 -> workerLoop(1)
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
// wait
//
// 모든 Worker Thread가 끝날 때까지 기다린다.
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
// workerLoop
//
// 각 Worker가 반복해서 Task를 가져온다.
//
// Queue가 비면 Worker는 종료한다.
//
// 아직은 "기다리는 Thread Pool"이 아니다.
// v0.6에서 이 구조를 발전시킬 예정이다.
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
        Task task;

        if (!m_taskQueue.tryPop(task))
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
// executeTask
//
// 아직 Task 자체에 함수가 들어있는 구조가 아니므로
// durationMs 동안 sleep해서 작업을 흉내낸다.
//
// 이후에는 Task가 실제 실행 가능한 함수 또는
// Job 객체를 갖도록 발전시킬 예정이다.
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
// pendingTaskCount
//
// 아직 실행되지 않고 Queue에서 기다리는
// Task 개수를 반환한다.
// ---------------------------------------------------------
std::size_t
CoreThreadPool::pendingTaskCount()
{
    return m_taskQueue.size();
}