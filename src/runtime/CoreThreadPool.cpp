#include "runtime/CoreThreadPool.hpp"

#include <chrono>
#include <iostream>
#include <thread>


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
      m_startBarrier(workerCount),
      m_computeSink(0)
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
// Round-Robin Task Distribution
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


    m_workSignal.notifyOne();


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
// ---------------------------------------------------------
void CoreThreadPool::beginExecution()
{
    m_startBarrier.release();
}


// ---------------------------------------------------------
// shutdown
// ---------------------------------------------------------
void CoreThreadPool::shutdown()
{
    m_shutdownRequested.store(
        true
    );


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
        std::size_t observedGeneration =
            m_workSignal.snapshot();


        Task task{};


        // 자기 Local Queue의 Back에서 가져온다.
        bool hasTask =
            worker
                .localQueue
                .tryPopBack(task);


        // 자기 일이 없다면 Steal
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


        // Task 실행
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


        // 모든 Task 완료
        if (
            m_shutdownRequested.load()
            &&
            m_remainingTasks.load() == 0
        )
        {
            break;
        }


        // Idle -> Sleep
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
// TaskType에 따라 실제 실행 방식을 결정한다.
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


    // -----------------------------------------------------
    // Sleep Task
    //
    // CPU 계산을 하지 않고 일정 시간 Waiting.
    // -----------------------------------------------------
    if (
        task.type
        == TaskType::Sleep
    )
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(
                task.workAmount
            )
        );
    }

    // -----------------------------------------------------
    // Compute Task
    //
    // 실제 CPU 계산 수행.
    // -----------------------------------------------------
    else if (
        task.type
        == TaskType::Compute
    )
    {
        std::uint64_t result =
            executeComputeKernel(
                task
            );


        // 결과를 실제 observable state에 반영한다.
        //
        // relaxed:
        // 이 값은 Thread Synchronization 용도가 아니라
        // 계산 제거 방지 목적이므로 강한 memory ordering이
        // 필요하지 않다.
        m_computeSink.fetch_xor(
            result,
            std::memory_order_relaxed
        );
    }


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
// executeComputeKernel
//
// CPU-Bound Synthetic Workload.
//
// 이전 계산 결과를 다음 계산이 계속 사용하기 때문에
// 단순한 독립 반복보다 실제 CPU 계산 부하를 만든다.
//
// 최종 결과가 m_computeSink에서 사용되므로
// Release Optimization에서도 전체 계산을
// 제거할 수 없다.
// ---------------------------------------------------------
std::uint64_t
CoreThreadPool::executeComputeKernel(
    const Task& task
)
{
    std::uint64_t value =
        0x9E3779B97F4A7C15ULL
        +
        static_cast<std::uint64_t>(
            task.id
        );


    for (
        std::uint64_t i = 0;
        i < task.workAmount;
        i++
    )
    {
        // xorshift 기반 정수 연산
        value ^=
            value >> 12;


        value ^=
            value << 25;


        value ^=
            value >> 27;


        value *=
            2685821657736338717ULL;
    }


    return value;
}


// ---------------------------------------------------------
// pendingTaskCount
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


// ---------------------------------------------------------
// Compute 결과 확인용
// ---------------------------------------------------------
std::uint64_t
CoreThreadPool::computeChecksum() const
{
    return m_computeSink.load(
        std::memory_order_relaxed
    );
}