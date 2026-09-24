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
// Round-Robin으로 Task를 Worker Local Deque에 배치.
//
// 성공하면 sleeping Worker 하나를 깨운다.
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


    // Queue에 Task가 공개되기 전에 먼저 증가.
    //
    // Worker가 매우 빠르게 Task를 가져가서
    // 완료해버리는 race를 막기 위함.
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


    std::cout
        << "[SCHEDULER] Task "
        << task.id
        << " -> Worker "
        << targetWorker
        << '\n';


    // -----------------------------------------------------
    // Task가 생겼으므로
    // 잠들어 있는 Worker 하나를 깨운다.
    // -----------------------------------------------------
    m_workSignal.notifyOne();


    return true;
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
// 이후 submit 차단.
//
// Local Queue 안의 기존 Task는 끝까지 처리.
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


    // shutdown 상태가 바뀌었으므로
    // 잠들어 있는 Worker 전부 깨움.
    m_workSignal.notifyAll();
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
// Work Stealing
//
// 자신의 Queue가 비어 있으면
// 다른 Worker의 FRONT에서 Task를 훔친다.
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


        // -------------------------------------------------
        // Victim Queue 하나를 실제로 검사한다.
        //
        // 성공 여부와 관계없이 시도 자체를 기록한다.
        // -------------------------------------------------
        thief.statistics.stealAttempts++;


        if (
            m_workers[candidate]
                .localQueue
                .tryPopFront(task)
        )
        {
            victimId = candidate;


            // 실제 Steal 성공
            thief.statistics.stolenTasks++;


            return true;
        }
    }


    return false;
}


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
        // -------------------------------------------------
        // 현재 Work Signal 상태 기억
        //
        // Task 탐색과 Sleep 사이에서 발생할 수 있는
        // Lost Wake-Up을 방지하기 위해 사용한다.
        // -------------------------------------------------
        std::size_t observedGeneration =
            m_workSignal.snapshot();


        Task task{};


        // -------------------------------------------------
        // 1. 자신의 Local Queue 확인
        //
        // Owner는 Deque BACK을 사용한다.
        // -------------------------------------------------
        bool hasTask =
            worker
                .localQueue
                .tryPopBack(task);


        // -------------------------------------------------
        // 2. 자기 Task가 없으면 Work Stealing
        // -------------------------------------------------
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


        // -------------------------------------------------
        // 3. Task 획득 성공
        // -------------------------------------------------
        if (hasTask)
        {
            executeTask(
                workerId,
                task
            );


            // 실제 실행을 완료한 Task 하나 기록
            worker.statistics.executedTasks++;


            std::size_t previousRemaining =
                m_remainingTasks.fetch_sub(1);


            // 내가 마지막 Task를 완료했다면
            // shutdown 상태에서 잠든 Worker들도
            // 종료 조건을 확인할 수 있도록 깨운다.
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
        // 4. Shutdown + 전체 Task 완료
        //
        // 더 이상 할 일이 없다.
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
        // 5. Task를 찾지 못했으므로 Sleep
        // -------------------------------------------------
        worker.statistics.sleepCount++;


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


        // waitForChange가 실제로 return했다.
        worker.statistics.wakeCount++;
    }


    std::cout
        << "[Worker "
        << workerId
        << "] stopped\n";
}


// ---------------------------------------------------------
// 실제 Task 실행
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
// Queue 안에 아직 대기 중인 Task 개수.
//
// 실행 중인 Task는 포함되지 않는다.
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

void CoreThreadPool::printStatistics() const
{
    std::size_t totalExecuted = 0;
    std::size_t totalStolen = 0;
    std::size_t totalStealAttempts = 0;
    std::size_t totalSleeps = 0;
    std::size_t totalWakes = 0;


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


        totalExecuted +=
            stats.executedTasks;

        totalStolen +=
            stats.stolenTasks;

        totalStealAttempts +=
            stats.stealAttempts;

        totalSleeps +=
            stats.sleepCount;

        totalWakes +=
            stats.wakeCount;
    }


    std::cout
        << "\n--- Total ---\n";


    std::cout
        << "Executed Tasks : "
        << totalExecuted
        << '\n';


    std::cout
        << "Stolen Tasks   : "
        << totalStolen
        << '\n';


    std::cout
        << "Steal Attempts : "
        << totalStealAttempts
        << '\n';


    std::cout
        << "Sleep Count    : "
        << totalSleeps
        << '\n';


    std::cout
        << "Wake Count     : "
        << totalWakes
        << '\n';
}