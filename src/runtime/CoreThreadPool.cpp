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


            // -------------------------------------------------
            // Worker별 deterministic seed
            //
            // 같은 Worker ID라도 0이 아닌 서로 다른
            // 초기 상태를 가지도록 만든다.
            // -------------------------------------------------
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


    // Queue에 Task를 공개하기 전에
    // 미완료 Task 수를 먼저 증가.
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
// nextRandom
//
// xorshift64*
//
// std::random을 사용하지 않고
// Worker별 PRNG를 직접 구현한다.
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
// trySteal
//
// 현재 StealPolicy에 맞는
// Victim Selection 알고리즘을 호출한다.
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
//
// thiefId 다음 Worker부터 고정된 순서로 탐색.
//
// 예:
//
// thief = 3
// workers = 8
//
// 4 -> 5 -> 6 -> 7 -> 0 -> 1 -> 2
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


            // 성공한 탐색이 몇 번의 Probe를 필요로 했는지 기록
            thief
                .statistics
                .successfulStealProbes
                += probesThisRound;


            return true;
        }
    }


    // 모든 Victim을 확인했지만 실패
    thief
        .statistics
        .failedStealRounds++;


    return false;
}


// ---------------------------------------------------------
// Random-Start Victim Search
//
// Victim 전체를 무작위 중복 방식으로 찍는 것이 아니라,
// "첫 번째 Victim"만 Random하게 정한다.
//
// 이후에는 원형 순서로 모든 Victim을 정확히 한 번씩
// 검사할 수 있다.
//
// 따라서 Sequential과 동일하게
// 최악의 경우 모든 Victim을 검사한다.
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


    // -----------------------------------------------------
    // offset 후보:
    //
    // 1 ~ workerCount - 1
    //
    // Random 값으로 이 목록의 시작 위치를 고른다.
    // -----------------------------------------------------
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
        // 0 ~ victimCount-1
        std::size_t offsetIndex =
            (
                startIndex
                +
                probe
            )
            % victimCount;


        // 실제 Worker offset은 1부터 시작
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
        std::size_t observedGeneration =
            m_workSignal.snapshot();


        Task task{};


        // -------------------------------------------------
        // 1. 자신의 Queue
        // -------------------------------------------------
        bool hasTask =
            worker
                .localQueue
                .tryPopBack(task);


        // -------------------------------------------------
        // 2. Work Stealing
        // -------------------------------------------------
        if (
            !hasTask
            &&
            m_mode
                == SchedulingMode::WorkStealing
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
        // 3. Task 실행
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
        // 5. Idle Sleep
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


        total.successfulStealProbes +=
            stats.successfulStealProbes;


        total.failedStealRounds +=
            stats.failedStealRounds;


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