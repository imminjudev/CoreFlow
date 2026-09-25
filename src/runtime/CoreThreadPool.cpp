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


            // Worker별 deterministic PRNG seed
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


            // Worker 전용 Compute Checksum 초기화
            m_workers[i].computeChecksum =
                0;
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


    // Task가 Queue에 공개되기 전에 증가
    m_remainingTasks.fetch_add(1);

    m_queuedTasks.fetch_add(1);


    bool success =
        m_workers[targetWorker]
            .localQueue
            .pushBack(task);


    // Queue가 닫혀 있다면 rollback
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


    // beginExecution 이전 shutdown 방어
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
// Worker별 PRNG
//
// xorshift64*
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


    // Benchmark Start Barrier
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


        // -------------------------------------------------
        // 1. 자신의 Local Queue
        // -------------------------------------------------
        bool hasTask =
            worker
                .localQueue
                .tryPopBack(task);


        if (hasTask)
        {
            m_queuedTasks.fetch_sub(1);
        }


        // -------------------------------------------------
        // 2. 모든 Task가 이미 끝났으면 바로 종료
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
            // 실제 Queue에 Task가 있을 때만 Scan
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


                    // Victim Queue에서 Task 하나 제거됨
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


            std::size_t previousRemaining =
                m_remainingTasks.fetch_sub(1);


            // 마지막 Task 완료
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
        // 5. Steal 탐색 중 마지막 Task가 끝났을 수도 있음
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
        // 6. Idle Wait
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


    // -----------------------------------------------------
    // Sleep Task
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


        // -------------------------------------------------
        // v0.20 핵심 변경
        //
        // 기존:
        //
        // global atomic.fetch_xor(...)
        //
        // 현재:
        //
        // 실행 Worker 자신의 일반 uint64_t만 수정.
        //
        // 이 Worker Thread만 해당 필드를 수정하므로
        // atomic synchronization이 필요 없다.
        // -------------------------------------------------
        m_workers[workerId]
            .computeChecksum
            ^= result;
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
// computeChecksum
//
// 각 Worker의 local checksum을 최종적으로 XOR.
//
// XOR은 순서에 관계없이 결과가 같다.
//
// A ^ B ^ C
//
// 와
//
// C ^ A ^ B
//
// 는 같은 결과가 나온다.
//
// 따라서 Task가 어느 Worker에서 실행되었는지와 관계없이
// 동일한 Task 집합이면 최종 checksum도 동일하다.
//
// 반드시 모든 Worker가 join된 뒤 호출하는 것을 전제로 한다.
// ---------------------------------------------------------
std::uint64_t
CoreThreadPool::computeChecksum() const
{
    std::uint64_t finalChecksum =
        0;


    for (
        std::size_t i = 0;
        i < m_workerCount;
        i++
    )
    {
        finalChecksum ^=
            m_workers[i]
                .computeChecksum;
    }


    return finalChecksum;
}