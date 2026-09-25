#pragma once

#include <cstddef>
#include <cstdint>
#include <thread>

#include "core/CoreBlockingDeque.hpp"
#include "runtime/Task.hpp"


struct WorkerStatistics
{
    // 실행 완료 Task 수
    std::size_t executedTasks = 0;

    // Steal 성공 Task 수
    std::size_t stolenTasks = 0;

    // 실제 Victim Queue를 확인한 횟수
    std::size_t stealAttempts = 0;

    // 성공한 Steal 탐색에서 사용된 Probe 수의 합
    std::size_t successfulStealProbes = 0;

    // 모든 Victim을 확인했지만 실패한 Steal Round
    std::size_t failedStealRounds = 0;

    // queuedTasks == 0이라
    // Victim Search 자체를 생략한 횟수
    std::size_t stealSkippedNoQueuedWork = 0;

    // Wait 경로 진입 / 복귀
    std::size_t sleepCount = 0;

    std::size_t wakeCount = 0;
};


struct WorkerState
{
    std::size_t id = 0;

    std::thread thread;

    CoreBlockingDeque<Task> localQueue;

    WorkerStatistics statistics;


    // -----------------------------------------------------
    // RandomStart Victim Selection용 PRNG 상태
    //
    // 이 Worker Thread만 수정한다.
    // -----------------------------------------------------
    std::uint64_t randomState = 1;


    // -----------------------------------------------------
    // v0.20
    //
    // 이 Worker가 실행한 Compute Task들의 결과를
    // Worker 자신의 checksum에 누적한다.
    //
    // 다른 Worker는 이 값을 수정하지 않으므로
    // atomic이 필요 없다.
    //
    // Main Thread에서는 모든 Worker join 이후
    // 읽는 것을 전제로 한다.
    // -----------------------------------------------------
    std::uint64_t computeChecksum = 0;
};