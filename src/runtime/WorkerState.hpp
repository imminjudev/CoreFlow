#pragma once

#include <cstddef>
#include <thread>

#include "core/CoreBlockingDeque.hpp"
#include "runtime/Task.hpp"


// ---------------------------------------------------------
// WorkerStatistics
//
// Worker 하나가 실행되는 동안 발생한
// Scheduler 관련 통계를 저장한다.
//
// 이 값들은 해당 Worker Thread만 수정하고,
// 모든 Worker가 join된 이후 Main Thread가 읽는다.
// ---------------------------------------------------------
struct WorkerStatistics
{
    // 이 Worker가 최종적으로 실행한 Task 수
    std::size_t executedTasks = 0;

    // 다른 Worker에게서 훔쳐 실행한 Task 수
    std::size_t stolenTasks = 0;

    // 다른 Worker Queue를 실제로 확인한 횟수
    std::size_t stealAttempts = 0;

    // Work Signal을 기다리며 Sleep에 들어간 횟수
    std::size_t sleepCount = 0;

    // Sleep에서 다시 돌아온 횟수
    std::size_t wakeCount = 0;
};


struct WorkerState
{
    std::size_t id = 0;

    std::thread thread;

    CoreBlockingDeque<Task> localQueue;

    WorkerStatistics statistics;
};