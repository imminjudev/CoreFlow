#include "runtime/CoreThreadPool.hpp"

#include <chrono>
#include <iostream>
#include <thread>


CoreThreadPool::CoreThreadPool(
    std::size_t workerCount
)
    : m_workers(nullptr),
      m_workerCount(workerCount),
      m_nextWorker(0),
      m_started(false),
      m_remainingTasks(0),
      m_shutdownRequested(false)
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
// Round-Robin으로 각 Worker의 Local Deque에 배치한다.
// ---------------------------------------------------------
bool CoreThreadPool::submit(
    const Task& task
)
{
    if (m_workerCount == 0)
    {
        return false;
    }

    if (m_shutdownRequested.load())
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


    // Task가 Queue에 공개되기 전에
    // remaining count를 먼저 증가시킨다.
    //
    // 그렇지 않으면 Worker가 매우 빠르게 Task를 가져가서
    // count 증가 전에 완료하는 race가 생길 수 있다.
    m_remainingTasks.fetch_add(1);


    bool success =
        m_workers[targetWorker]
            .localQueue
            .pushBack(task);


    if (!success)
    {
        // Queue가 이미 close된 경우 원상복구
        m_remainingTasks.fetch_sub(1);

        return false;
    }


    std::cout
        << "[SCHEDULER] Task "
        << task.id
        << " -> Worker "
        << targetWorker
        << '\n';


    return true;
}


// ---------------------------------------------------------
// start
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
// shutdown
//
// 새로운 Task 제출을 막고 Local Queue들을 닫는다.
//
// 이미 들어간 Task는 모두 실행한다.
// ---------------------------------------------------------
void CoreThreadPool::shutdown()
{
    m_shutdownRequested.store(true);


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
// wait
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
// trySteal
//
// 자기 Queue에 일이 없으면 다른 Worker들을 순서대로 확인한다.
//
// 다른 Worker의 FRONT에서 Task를 가져온다.
//
// Owner:
//      popBack()
//
// Thief:
//      popFront()
//
// 서로 반대쪽을 주로 사용하게 한다.
// ---------------------------------------------------------
bool CoreThreadPool::trySteal(
    std::size_t thiefId,
    Task& task,
    std::size_t& victimId
)
{
    if (m_workerCount <= 1)
    {
        return false;
    }


    for (
        std::size_t offset = 1;
        offset < m_workerCount;
        offset++
    )
    {
        std::size_t candidate =
            (thiefId + offset)
            % m_workerCount;


        if (
            m_workers[candidate]
                .localQueue
                .tryPopFront(task)
        )
        {
            victimId = candidate;

            return true;
        }
    }


    return false;
}


// ---------------------------------------------------------
// workerLoop
//
// 1. 자기 Local Queue 뒤에서 Task 획득
// 2. 없으면 다른 Worker Queue 앞에서 Steal
// 3. 그래도 없으면 잠깐 CPU 실행권 양보
// 4. shutdown + 모든 Task 완료 시 종료
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

        bool hasTask =
            worker
                .localQueue
                .tryPopBack(task);


        // ---------------------------------------------
        // Local Queue가 비었다면 Steal 시도
        // ---------------------------------------------
        if (!hasTask)
        {
            std::size_t victimId = 0;


            if (
                trySteal(
                    workerId,
                    task,
                    victimId
                )
            )
            {
                hasTask = true;


                std::cout
                    << "[STEAL] Worker "
                    << workerId
                    << " stole Task "
                    << task.id
                    << " from Worker "
                    << victimId
                    << '\n';
            }
        }


        // ---------------------------------------------
        // Task 획득 성공
        // ---------------------------------------------
        if (hasTask)
        {
            executeTask(
                workerId,
                task
            );


            // Task 하나 완전히 처리 완료
            m_remainingTasks.fetch_sub(1);


            continue;
        }


        // ---------------------------------------------
        // 더 이상 Task가 없고 shutdown도 요청됐다면 종료
        // ---------------------------------------------
        if (
            m_shutdownRequested.load()
            &&
            m_remainingTasks.load() == 0
        )
        {
            break;
        }


        // 아직 다른 Worker가 실행 중일 수도 있고,
        // 앞으로 Task가 들어올 수도 있다.
        //
        // 지금 버전에서는 busy-spin을 줄이기 위해
        // CPU 실행권을 다른 Thread에게 양보한다.
        std::this_thread::yield();
    }


    std::cout
        << "[Worker "
        << workerId
        << "] stopped\n";
}


// ---------------------------------------------------------
// executeTask
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
// 각 Local Queue에 아직 들어있는 Task 개수
//
// 실행 중인 Task는 포함하지 않는다.
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