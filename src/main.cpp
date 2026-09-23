#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

#include "core/CoreQueue.hpp"
#include "core/CoreSpinLock.hpp"
#include "runtime/Task.hpp"


// ---------------------------------------------------------
// Task 실행
//
// 현재는 실제 계산 대신 sleep_for()를 사용해서
// Task가 일정 시간 동안 실행되는 상황을 흉내낸다.
//
// workerId를 받아서 어떤 Worker가 Task를 실행했는지
// 출력한다.
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
// Worker Thread가 실행할 함수
//
// 여러 Worker가 하나의 Queue를 공유한다.
//
// Queue에서 Task를 꺼내는 순간에는
// CoreSpinLock을 이용해서 한 Worker만 접근할 수 있게 한다.
//
// Task 실행 자체에는 Lock을 걸지 않는다.
// 그래야 여러 Worker가 동시에 Task를 실행할 수 있다.
// ---------------------------------------------------------
void worker(
    int workerId,
    CoreQueue<Task>& taskQueue,
    CoreSpinLock& queueLock
)
{
    std::cout
        << "[Worker "
        << workerId
        << "] started\n";

    while (true)
    {
        Task task;

        // -----------------------------
        // Critical Section 시작
        //
        // Queue 자체는 아직 Thread-Safe하지 않으므로
        // Task를 꺼낼 때 Lock이 필요하다.
        // -----------------------------
        queueLock.lock();

        bool hasTask =
            taskQueue.tryPop(task);

        queueLock.unlock();

        // -----------------------------
        // Critical Section 끝
        // -----------------------------

        // Queue가 비어 있으면 Worker 종료
        if (!hasTask)
        {
            break;
        }

        // Lock을 해제한 뒤 Task 실행
        //
        // 따라서 다른 Worker도 동시에
        // Queue에서 다음 Task를 가져갈 수 있다.
        executeTask(workerId, task);
    }

    std::cout
        << "[Worker "
        << workerId
        << "] stopped\n";
}


int main()
{
    std::cout
        << "=== CoreFlow v0.2 ===\n\n";


    // -----------------------------------------------------
    // 우리가 직접 만든 FIFO Queue
    // -----------------------------------------------------
    CoreQueue<Task> taskQueue;


    // -----------------------------------------------------
    // Queue 보호용 Spin Lock
    // -----------------------------------------------------
    CoreSpinLock queueLock;


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
    // 실행시간 측정 시작
    // -----------------------------------------------------
    auto start =
        std::chrono::steady_clock::now();


    // -----------------------------------------------------
    // Worker Thread 2개 생성
    //
    // 두 Worker 모두 같은 taskQueue와 queueLock을 공유한다.
    //
    // std::ref()를 사용하는 이유:
    // Queue와 Lock을 복사하지 않고 원본을 전달하기 위함.
    // -----------------------------------------------------
    std::thread worker0(
        worker,
        0,
        std::ref(taskQueue),
        std::ref(queueLock)
    );

    std::thread worker1(
        worker,
        1,
        std::ref(taskQueue),
        std::ref(queueLock)
    );


    // -----------------------------------------------------
    // 두 Worker가 모두 끝날 때까지 Main Thread 대기
    // -----------------------------------------------------
    worker0.join();
    worker1.join();


    // -----------------------------------------------------
    // 실행시간 측정 종료
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