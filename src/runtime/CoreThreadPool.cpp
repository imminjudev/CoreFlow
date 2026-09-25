#include "runtime/CoreThreadPool.hpp"

#include <chrono>
#include <iostream>
#include <thread>


// ---------------------------------------------------------
// Constructor
// ---------------------------------------------------------
CoreThreadPool::CoreThreadPool(
    std::size_t workerCount,
    SchedulingMode mode,
    StealPolicy stealPolicy,
    bool verbose
)
    : m_workers(nullptr),
      m_workerCount(workerCount),
      m_nextWorker(0),
      m_started(false),
      m_verbose(verbose),
      m_mode(mode),
      m_stealPolicy(stealPolicy),
      m_remainingTasks(0),
      m_queuedTasks(0),
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


            std::uint64_t seed =
                0x9E3779B97F4A7C15ULL
                ^
                (
                    static_cast<std::uint64_t>(
                        i + 1
                    )
                    *
                    0xD1B54A32D192ED03ULL
                );


            if (seed == 0)
            {
                seed = 1;
            }


            m_workers[i].randomState =
                seed;
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


    // -----------------------------------------------------
    // Task를 Queue에 공개하기 전에 증가시킨다.
    //
    // Worker가 push 직후 Task를 매우 빠르게 가져가는
    // Race를 막기 위한 순서다.
    // -----------------------------------------------------
    m_remainingTasks.fetch_add(1);

    m_queuedTasks.fetch_add(1);


    bool success =
        m_workers[targetWorker]
            .localQueue
            .pushBack(task);


    // Queue가 이미 Close된 경우 rollback
    if (!success)
    {
        m_queuedTasks.fetch_sub(1);

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
// Worker 전용 xorshift64*
// ---------------------------------------------------------
std::uint64_t
CoreThreadPool::nextRandom(
    std::size_t workerId
)
{
    std::uint64_t value =
        m_workers[workerId]
            .randomState;


    value ^=
        value >> 12;


    value ^=
        value << 25;


    value ^=
        value >> 27;


    m_workers[workerId].randomState =
        value;


    return
        value
        *
        2685821657736338717ULL;
}


// ---------------------------------------------------------
// Steal Policy 선택
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


    if (
        m_stealPolicy
        == StealPolicy::RandomStart
    )
    {
        return
            tryStealRandomStart(
                thiefId,
                task,
                victimId
            );
    }


    return
        tryStealSequential(
            thiefId,
            task,
            victimId
        );
}


// ---------------------------------------------------------
// Sequential Victim Search
// ---------------------------------------------------------
bool CoreThreadPool::tryStealSequential(
    std::size_t thiefId,
    Task& task,
    std::size_t& victimId
)
{
    WorkerState& thief =
        m_workers[thiefId];


    std::size_t probesThisRound =
        0;


    for (
        std::size_t offset = 1;
        offset < m_workerCount;
        offset++
    )
    {
        std::size_t candidate =
            (thiefId + offset)
            % m_workerCount;


        probesThisRound++;


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


            thief
                .statistics
                .successfulStealProbes
                += probesThisRound;


            return true;
        }
    }


    thief
        .statistics
        .failedStealRounds++;


    return false;
}


// ---------------------------------------------------------
// Random-Start Victim Search
// ---------------------------------------------------------
bool CoreThreadPool::tryStealRandomStart(
    std::size_t thiefId,
    Task& task,
    std::size_t& victimId
)
{
    WorkerState& thief =
        m_workers[thiefId];


    const std::size_t victimCount =
        m_workerCount - 1;


    std::size_t startIndex =
        static_cast<std::size_t>(
            nextRandom(thiefId)
            % victimCount
        );


    std::size_t probesThisRound =
        0;


    for (
        std::size_t probe = 0;
        probe < victimCount;
        probe++
    )
    {
        std::size_t offsetIndex =
            (
                startIndex
                +
                probe
            )
            % victimCount;


        std::size_t offset =
            offsetIndex + 1;


        std::size_t candidate =
            (
                thiefId
                +
                offset
            )
            % m_workerCount;


        probesThisRound++;


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


            thief
                .statistics
                .successfulStealProbes
                += probesThisRound;


            return true;
        }
    }


    thief
        .statistics
        .failedStealRounds++;


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
        // Lost Wake-Up 방지용
        std::size_t observedGeneration =
            m_workSignal.snapshot();


        Task task{};


        // -------------------------------------------------
        // 1. 자신의 Local Queue에서 Task 획득
        // -------------------------------------------------
        bool hasTask =
            worker
                .localQueue
                .tryPopBack(task);


        if (hasTask)
        {
            // Queue에서 Task 하나가 빠졌으므로 감소
            m_queuedTasks.fetch_sub(1);
        }


        // -------------------------------------------------
        // 2. Local Queue가 비었다면 종료 여부를 먼저 확인
        //
        // 모든 Task가 이미 완료된 상태라면
        // Steal Scan조차 할 이유가 없다.
        // -------------------------------------------------
        if (
            !hasTask
            &&
            m_shutdownRequested.load()
            &&
            m_remainingTasks.load() == 0
        )
        {
            break;
        }


        // -------------------------------------------------
        // 3. Work Stealing
        // -------------------------------------------------
        if (
            !hasTask
            &&
            m_mode
                == SchedulingMode::WorkStealing
        )
        {
            // -------------------------------------------------
            // 핵심:
            //
            // 아직 Queue에 Task가 하나라도 있을 때만
            // Victim Search를 수행한다.
            //
            // queuedTasks == 0이라면
            //
            // remainingTasks > 0
            //
            // 이어도 모든 Task가 이미 다른 Worker에서
            // 실행 중이라는 뜻이다.
            // -------------------------------------------------
            if (
                m_queuedTasks.load() > 0
            )
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


                    // Steal 또한 Queue에서 Task를 제거한 것
                    m_queuedTasks.fetch_sub(1);


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
            else
            {
                // -------------------------------------------------
                // v0.17이었다면 여기서 Victim Queue 전체를
                // 검사했을 상황.
                //
                // v0.18에서는 바로 Scan을 생략한다.
                // -------------------------------------------------
                worker
                    .statistics
                    .stealSkippedNoQueuedWork++;
            }
        }


        // -------------------------------------------------
        // 4. Task 실행
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


            // 실행 완료
            std::size_t previousRemaining =
                m_remainingTasks.fetch_sub(1);


            // 마지막 Task 종료
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
        // 5. Steal 도중 다른 Worker가 마지막 Task를
        //    끝냈을 가능성이 있으므로 다시 확인
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
        // 6. Idle -> Wait
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
    else if (
        task.type
        == TaskType::Compute
    )
    {
        std::uint64_t result =
            executeComputeKernel(
                task
            );


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
// CPU Compute Kernel
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
//
// 각 Local Queue 크기를 합산.
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
// queuedTaskCount
//
// m_queuedTasks의 현재 값.
// ---------------------------------------------------------
std::size_t
CoreThreadPool::queuedTaskCount() const
{
    return m_queuedTasks.load();
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


        total.successfulStealProbes +=
            stats.successfulStealProbes;


        total.failedStealRounds +=
            stats.failedStealRounds;


        total.stealSkippedNoQueuedWork +=
            stats.stealSkippedNoQueuedWork;


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
            << "  Executed Tasks          : "
            << stats.executedTasks
            << '\n';


        std::cout
            << "  Stolen Tasks            : "
            << stats.stolenTasks
            << '\n';


        std::cout
            << "  Steal Attempts          : "
            << stats.stealAttempts
            << '\n';


        std::cout
            << "  Successful Steal Probes : "
            << stats.successfulStealProbes
            << '\n';


        std::cout
            << "  Failed Steal Rounds     : "
            << stats.failedStealRounds
            << '\n';


        std::cout
            << "  Skipped Empty Scans     : "
            << stats.stealSkippedNoQueuedWork
            << '\n';


        std::cout
            << "  Sleep Count             : "
            << stats.sleepCount
            << '\n';


        std::cout
            << "  Wake Count              : "
            << stats.wakeCount
            << '\n';
    }


    CoreSchedulerStatistics total =
        getStatistics();


    std::cout
        << "\n--- Total ---\n";


    std::cout
        << "Executed Tasks          : "
        << total.executedTasks
        << '\n';


    std::cout
        << "Stolen Tasks            : "
        << total.stolenTasks
        << '\n';


    std::cout
        << "Steal Attempts          : "
        << total.stealAttempts
        << '\n';


    std::cout
        << "Successful Steal Probes : "
        << total.successfulStealProbes
        << '\n';


    std::cout
        << "Failed Steal Rounds     : "
        << total.failedStealRounds
        << '\n';


    std::cout
        << "Skipped Empty Scans     : "
        << total.stealSkippedNoQueuedWork
        << '\n';


    std::cout
        << "Sleep Count             : "
        << total.sleepCount
        << '\n';


    std::cout
        << "Wake Count              : "
        << total.wakeCount
        << '\n';
}


// ---------------------------------------------------------
// Compute Checksum
// ---------------------------------------------------------
std::uint64_t
CoreThreadPool::computeChecksum() const
{
    return m_computeSink.load(
        std::memory_order_relaxed
    );
}