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

    // -----------------------------------------------------
    // queuedTasks == 0이라서
    // Victim Search 자체를 생략한 횟수
    // -----------------------------------------------------
    std::size_t stealSkippedNoQueuedWork = 0;

    std::size_t sleepCount = 0;

    std::size_t wakeCount = 0;
};


struct WorkerState
{
    std::size_t id = 0;

    std::thread thread;

    CoreBlockingDeque<Task> localQueue;

    WorkerStatistics statistics;


    // RandomStart Victim Selection용
    // Worker 전용 PRNG state
    std::uint64_t randomState = 1;
};