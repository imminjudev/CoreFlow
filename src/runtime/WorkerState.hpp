#pragma once

#include <cstddef>
#include <cstdint>
#include <thread>

#include "core/CoreBlockingDeque.hpp"
#include "runtime/Task.hpp"


// ---------------------------------------------------------
// WorkerStatistics
//
// 각 Worker 자신의 Thread만 수정한다.
//
// Main Thread에서는 pool.wait() 이후 읽으므로
// 각 필드를 atomic으로 만들 필요가 없다.
// ---------------------------------------------------------
struct WorkerStatistics
{
    // 실행 완료한 Task
    std::size_t executedTasks = 0;

    // 다른 Worker에게서 훔친 Task
    std::size_t stolenTasks = 0;

    // Victim Queue를 실제 확인한 총 횟수
    std::size_t stealAttempts = 0;

    // -----------------------------------------------------
    // Steal에 성공한 탐색들에서 사용한 Probe 수의 합.
    //
    // 예:
    //
    // W1 확인 -> 실패
    // W2 확인 -> 실패
    // W3 확인 -> 성공
    //
    // successfulStealProbes += 3
    // -----------------------------------------------------
    std::size_t successfulStealProbes = 0;

    // 모든 Victim Queue를 검사했지만
    // Task를 하나도 찾지 못한 Steal Round 수
    std::size_t failedStealRounds = 0;

    // Wait 경로 진입 횟수
    std::size_t sleepCount = 0;

    // Wait에서 복귀한 횟수
    std::size_t wakeCount = 0;
};


// ---------------------------------------------------------
// WorkerState
// ---------------------------------------------------------
struct WorkerState
{
    std::size_t id = 0;

    std::thread thread;

    CoreBlockingDeque<Task> localQueue;

    WorkerStatistics statistics;


    // -----------------------------------------------------
    // Worker 전용 PRNG 상태
    //
    // Random-Start Victim Selection에서 사용.
    //
    // 이 값도 해당 Worker Thread만 수정한다.
    // -----------------------------------------------------
    std::uint64_t randomState = 1;
};