#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/CoreLockGuard.hpp"
#include "core/CoreSpinLock.hpp"
#include "core/CoreStartBarrier.hpp"
#include "core/CoreWorkSignal.hpp"

#include "runtime/Task.hpp"
#include "runtime/WorkerState.hpp"


// ---------------------------------------------------------
// Thread Pool 전체 Scheduler 통계
// ---------------------------------------------------------
struct CoreSchedulerStatistics
{
    std::size_t executedTasks = 0;

    std::size_t stolenTasks = 0;

    std::size_t stealAttempts = 0;

    std::size_t sleepCount = 0;

    std::size_t wakeCount = 0;
};


class CoreThreadPool
{
private:
    // Worker 배열
    WorkerState* m_workers;

    // Worker 개수
    std::size_t m_workerCount;

    // Round-Robin 배치용
    std::size_t m_nextWorker;

    // submit() 보호
    CoreSpinLock m_submitLock;

    // Thread Pool 시작 여부
    bool m_started;

    // 로그 출력 여부
    bool m_verbose;


    // -----------------------------------------------------
    // Queue에 대기 중이거나 실행 중인
    // 전체 미완료 Task 수
    // -----------------------------------------------------
    std::atomic<std::size_t> m_remainingTasks;


    // shutdown 요청 여부
    std::atomic<bool> m_shutdownRequested;


    // Worker Sleep / Wake
    CoreWorkSignal m_workSignal;


    // Benchmark 시작 Barrier
    CoreStartBarrier m_startBarrier;


    // -----------------------------------------------------
    // CPU Compute Task 결과 저장
    //
    // Compute 결과를 실제 observable state에 저장해서
    // Release 최적화 시 계산 전체가 제거되는 것을 방지한다.
    // -----------------------------------------------------
    std::atomic<std::uint64_t> m_computeSink;


private:
    // Worker Scheduling Loop
    void workerLoop(
        std::size_t workerId
    );


    // 다른 Worker의 Task Steal
    bool trySteal(
        std::size_t thiefId,
        Task& task,
        std::size_t& victimId
    );


    // Task 실행
    void executeTask(
        std::size_t workerId,
        const Task& task
    );


    // -----------------------------------------------------
    // 실제 CPU 연산 Kernel
    //
    // Task의 workAmount만큼 반복 계산한다.
    // -----------------------------------------------------
    std::uint64_t executeComputeKernel(
        const Task& task
    );


public:
    explicit CoreThreadPool(
        std::size_t workerCount,
        bool verbose = true
    );


    ~CoreThreadPool();


    CoreThreadPool(
        const CoreThreadPool&
    ) = delete;


    CoreThreadPool& operator=(
        const CoreThreadPool&
    ) = delete;


    // Task 제출
    bool submit(
        const Task& task
    );


    // Worker 생성
    void start();


    // Start Barrier 개방
    void beginExecution();


    // 추가 Task 제출 차단
    void shutdown();


    // Worker 종료 대기
    void wait();


    // Queue 안에 대기 중인 Task 개수
    std::size_t pendingTaskCount();


    // 전체 Scheduler 통계
    CoreSchedulerStatistics
        getStatistics() const;


    // Scheduler 통계 출력
    void printStatistics() const;


    // -----------------------------------------------------
    // CPU Compute 결과 확인
    //
    // Benchmark 결과값으로 쓰는 건 아니고,
    // 계산 결과가 실제 프로그램에 사용되도록 하기 위한 값.
    // -----------------------------------------------------
    std::uint64_t
        computeChecksum() const;
};