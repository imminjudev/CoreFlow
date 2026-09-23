#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

#include "core/CoreThreadSafeQueue.hpp"
#include "runtime/Task.hpp"


// ---------------------------------------------------------
// Task 실행 함수
// ---------------------------------------------------------
void executeTask(int workerId, const Task& task)
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
        std::chrono::milliseconds(task.durationMs)
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
// Worker Thread
//
// 이제 Worker는
// CoreSpinLock이나 CoreLockGuard를 전혀 알 필요가 없다.
//
// CoreThreadSafeQueue가 내부적으로
// Queue 접근을 동기화한다.
// ---------------------------------------------------------
void worker(
    int workerId,
    CoreThreadSafeQueue<Task>& taskQueue
)
{
    std::cout
        << "[Worker "
        << workerId
        << "] started\n";

    while (true)
    {
        Task task;

        // Thread-safe Queue에서 Task 하나를 꺼낸다.
        if (!taskQueue.tryPop(task))
        {
            break;
        }

        executeTask(
            workerId,
            task
        );
    }

    std::cout
        << "[Worker "
        << workerId
        << "] stopped\n";
}


int main()
{
    std::cout
        << "=== CoreFlow v0.4 ===\n\n";


    // -----------------------------------------------------
    // Thread-Safe Task Queue
    //
    // 내부 구조:
    //
    // CoreThreadSafeQueue
    //      |
    //      +-- CoreQueue
    //      |
    //      +-- CoreSpinLock
    //      |
    //      +-- CoreLockGuard
    //
    // main.cpp에서는 이 내부 구현을 몰라도 된다.
    // -----------------------------------------------------
    CoreThreadSafeQueue<Task> taskQueue;


    // -----------------------------------------------------
    // Task 등록
    // -----------------------------------------------------
    taskQueue.push(
        {1, "Compile", 1000}
    );

    taskQueue.push(
        {2, "Physics", 600}
    );

    taskQueue.push(
        {3, "AI", 800}
    );

    taskQueue.push(
        {4, "Audio", 400}
    );

    taskQueue.push(
        {5, "Render", 1200}
    );


    std::cout
        << "[MAIN] "
        << taskQueue.size()
        << " tasks submitted\n\n";


    // -----------------------------------------------------
    // 실행 시간 측정 시작
    // -----------------------------------------------------
    auto start =
        std::chrono::steady_clock::now();


    // -----------------------------------------------------
    // Worker Thread 2개 생성
    //
    // 두 Worker가 같은 Thread-Safe Queue를 공유한다.
    // -----------------------------------------------------
    std::thread worker0(
        worker,
        0,
        std::ref(taskQueue)
    );

    std::thread worker1(
        worker,
        1,
        std::ref(taskQueue)
    );


    // -----------------------------------------------------
    // 모든 Worker 종료 대기
    // -----------------------------------------------------
    worker0.join();
    worker1.join();


    // -----------------------------------------------------
    // 실행 시간 측정 종료
    // -----------------------------------------------------
    auto end =
        std::chrono::steady_clock::now();

    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            end - start
        );


    std::cout
        << "\nExecution time: "
        << elapsed.count()
        << " ms\n";

    std::cout
        << "\nAll tasks completed.\n";


    return 0;
}