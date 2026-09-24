#include "runtime/CoreThreadPool.hpp"

#include <chrono>
#include <iostream>
#include <thread>


// ---------------------------------------------------------
// Constructor
// ---------------------------------------------------------
CoreThreadPool::CoreThreadPool(
    std::size_t workerCount,
    bool verbose
)
    : m_workers(nullptr),
      m_workerCount(workerCount),
      m_nextWorker(0),
      m_started(false),
      m_verbose(verbose),
      m_remainingTasks(0),
      m_shutdownRequested(false),
      m_startBarrier(workerCount)
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
            m_workers[i].id =
                i;
        }
    }
}


// ---------------------------------------------------------
// Destructor
// ---------------------------------------------------------
CoreThreadPool::~CoreThreadPool()
{
    if (m_started)
    {
        shutdown();

        wait();
    }


    delete[] m_workers;


    m_workers =
        nullptr;
}


// ---------------------------------------------------------
// submit
//
// Round-Robin 방식으로 Task를
// 각 Worker의 Local Deque에 배치한다.
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
    // 미완료 Task 수를 먼저 증가시킨다.
    m_remainingTasks.fetch_add(1);


    bool success =
        m_workers[targetWorker]
            .localQueue
            .pushBack(task);


    if (!success)
    {
        m_remainingTasks.fetch_sub(1);

        return false;
    }


    if (m_verbose)
    {
        std::cout
            << "[SCHEDULER] Task "
            << task.id
            << " -> Worker "
            << targetWorker
            << '\n';
    }


    // Sleeping Worker 하나 깨우기
    m_workSignal.notifyOne();


    return true;
}


// ---------------------------------------------------------
// start
//
// Worker Thread 생성.
//
// 모든 Worker가 Start Barrier에
// 도착할 때까지 Main Thread도 기다린다.
// ---------------------------------------------------------
void CoreThreadPool::start()
{
    if (m_started)
    {
        return;
    }


    m_started =
        true;


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


    m_startBarrier.waitUntilReady();
}


// ---------------------------------------------------------
// beginExecution
//
// Start Barrier 개방.
// ---------------------------------------------------------
void CoreThreadPool::beginExecution()
{
    m_startBarrier.release();
}


// ---------------------------------------------------------
// shutdown
//
// 추가 Task 제출을 막는다.
//
// 이미 제출된 Task는 모두 처리한다.
// ---------------------------------------------------------
void CoreThreadPool::shutdown()
{
    m_shutdownRequested.store(
        true
    );


    // beginExecution() 전에 shutdown되어도
    // Barrier에서 영원히 기다리지 않도록 한다.
    m_startBarrier.release();


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


    // 잠든 Worker가 종료 조건을 확인하도록
    // 모두 깨운다.
    m_workSignal.notifyAll();
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


    m_started =
        false;
}


// ---------------------------------------------------------
// trySteal
//
// 자신의 Local Queue가 비었다면
// 다른 Worker의 FRONT에서 Task를 훔친다.
//
// Owner:
//      popBack()
//
// Thief:
//      popFront()
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


    WorkerState& thief =
        m_workers[thiefId];


    for (
        std::size_t offset = 1;
        offset < m_workerCount;
        offset++
    )
    {
        std::size_t candidate =
            (thiefId + offset)
            % m_workerCount;


        thief
            .statistics
            .stealAttempts++;


        if (
            m_workers[candidate]
                .localQueue
                .tryPopFront(task)
        )
        {
            victimId =
                candidate;


            thief
                .statistics
                .stolenTasks++;


            return true;
        }
    }


    return false;
}


// ---------------------------------------------------------
// workerLoop
//
// Worker Scheduling Policy:
//
// 1. Start Barrier
// 2. Local popBack()
// 3. 실패하면 Work Stealing
// 4. Task 실행
// 5. 일이 없으면 Sleep
// 6. 새로운 일이 생기면 Wake
// 7. 모든 Task 완료 후 종료
// ---------------------------------------------------------
void CoreThreadPool::workerLoop(
    std::size_t workerId
)
{
    if (m_verbose)
    {
        std::cout
            << "[Worker "
            << workerId
            << "] ready\n";
    }


    m_startBarrier.arriveAndWait();


    if (m_verbose)
    {
        std::cout
            << "[Worker "
            << workerId
            << "] started\n";
    }


    WorkerState& worker =
        m_workers[workerId];


    while (true)
    {
        // Lost Wake-Up 방지용
        std::size_t observedGeneration =
            m_workSignal.snapshot();


        Task task{};


        // -------------------------------------------------
        // 1. 자신의 Local Queue
        // -------------------------------------------------
        bool hasTask =
            worker
                .localQueue
                .tryPopBack(task);


        // -------------------------------------------------
        // 2. Local Queue가 비어 있다면 Steal
        // -------------------------------------------------
        if (!hasTask)
        {
            std::size_t victimId =
                0;


            if (
                trySteal(
                    workerId,
                    task,
                    victimId
                )
            )
            {
                hasTask =
                    true;


                if (m_verbose)
                {
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
        }


        // -------------------------------------------------
        // 3. Task 획득 성공
        // -------------------------------------------------
        if (hasTask)
        {
            executeTask(
                workerId,
                task
            );


            worker
                .statistics
                .executedTasks++;


            std::size_t previousRemaining =
                m_remainingTasks.fetch_sub(1);


            // 마지막 Task를 끝낸 Worker라면
            // 다른 Worker를 깨워 종료 조건 확인
            if (
                previousRemaining == 1
                &&
                m_shutdownRequested.load()
            )
            {
                m_workSignal.notifyAll();
            }


            continue;
        }


        // -------------------------------------------------
        // 4. 종료 조건
        // -------------------------------------------------
        if (
            m_shutdownRequested.load()
            &&
            m_remainingTasks.load() == 0
        )
        {
            break;
        }


        // -------------------------------------------------
        // 5. 할 일이 없으면 Sleep
        // -------------------------------------------------
        worker
            .statistics
            .sleepCount++;


        m_workSignal.waitForChange(
            observedGeneration,
            [this]()
            {
                return
                    m_shutdownRequested.load()
                    &&
                    m_remainingTasks.load() == 0;
            }
        );


        worker
            .statistics
            .wakeCount++;
    }


    if (m_verbose)
    {
        std::cout
            << "[Worker "
            << workerId
            << "] stopped\n";
    }
}


// ---------------------------------------------------------
// executeTask
//
// 현재 Benchmark Task는 durationMs만큼
// Sleep하는 Synthetic Task다.
// ---------------------------------------------------------
void CoreThreadPool::executeTask(
    std::size_t workerId,
    const Task& task
)
{
    if (m_verbose)
    {
        std::cout
            << "[Worker "
            << workerId
            << "] START Task "
            << task.id
            << " | "
            << task.name
            << '\n';
    }


    std::this_thread::sleep_for(
        std::chrono::milliseconds(
            task.durationMs
        )
    );


    if (m_verbose)
    {
        std::cout
            << "[Worker "
            << workerId
            << "] END   Task "
            << task.id
            << " | "
            << task.name
            << '\n';
    }
}


// ---------------------------------------------------------
// pendingTaskCount
//
// Queue 안에서 기다리는 Task만 센다.
// 실행 중인 Task는 포함하지 않는다.
// ---------------------------------------------------------
std::size_t
CoreThreadPool::pendingTaskCount()
{
    std::size_t total =
        0;


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


// ---------------------------------------------------------
// getStatistics
//
// 모든 Worker 통계를 합산하여 반환.
//
// Benchmark에서는 pool.wait() 이후 호출한다.
// ---------------------------------------------------------
CoreSchedulerStatistics
CoreThreadPool::getStatistics() const
{
    CoreSchedulerStatistics total{};


    for (
        std::size_t i = 0;
        i < m_workerCount;
        i++
    )
    {
        const WorkerStatistics& stats =
            m_workers[i].statistics;


        total.executedTasks +=
            stats.executedTasks;


        total.stolenTasks +=
            stats.stolenTasks;


        total.stealAttempts +=
            stats.stealAttempts;


        total.sleepCount +=
            stats.sleepCount;


        total.wakeCount +=
            stats.wakeCount;
    }


    return total;
}


// ---------------------------------------------------------
// printStatistics
// ---------------------------------------------------------
void CoreThreadPool::printStatistics() const
{
    std::cout
        << "\n=== Scheduler Statistics ===\n";


    for (
        std::size_t i = 0;
        i < m_workerCount;
        i++
    )
    {
        const WorkerStatistics& stats =
            m_workers[i].statistics;


        std::cout
            << "\nWorker "
            << i
            << '\n';


        std::cout
            << "  Executed Tasks : "
            << stats.executedTasks
            << '\n';


        std::cout
            << "  Stolen Tasks   : "
            << stats.stolenTasks
            << '\n';


        std::cout
            << "  Steal Attempts : "
            << stats.stealAttempts
            << '\n';


        std::cout
            << "  Sleep Count    : "
            << stats.sleepCount
            << '\n';


        std::cout
            << "  Wake Count     : "
            << stats.wakeCount
            << '\n';
    }


    CoreSchedulerStatistics total =
        getStatistics();


    std::cout
        << "\n--- Total ---\n";


    std::cout
        << "Executed Tasks : "
        << total.executedTasks
        << '\n';


    std::cout
        << "Stolen Tasks   : "
        << total.stolenTasks
        << '\n';


    std::cout
        << "Steal Attempts : "
        << total.stealAttempts
        << '\n';


    std::cout
        << "Sleep Count    : "
        << total.sleepCount
        << '\n';


    std::cout
        << "Wake Count     : "
        << total.wakeCount
        << '\n';
}