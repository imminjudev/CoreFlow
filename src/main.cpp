#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

#include "core/CoreLockGuard.hpp"
#include "core/CoreQueue.hpp"
#include "core/CoreSpinLock.hpp"
#include "runtime/Task.hpp"


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

        bool hasTask = false;

        // 이 블록 안에 들어오면 lock()
        // 블록을 벗어나면 자동으로 unlock()
        {
            CoreLockGuard<CoreSpinLock> guard(queueLock);

            hasTask = taskQueue.tryPop(task);
        }

        if (!hasTask)
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
        << "=== CoreFlow v0.3 ===\n\n";

    CoreQueue<Task> taskQueue;

    CoreSpinLock queueLock;


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


    auto start =
        std::chrono::steady_clock::now();


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


    worker0.join();
    worker1.join();


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