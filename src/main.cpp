#include <chrono>
#include <iostream>
#include <thread>

#include "runtime/CoreThreadPool.hpp"


int main()
{
    std::cout
        << "=== CoreFlow v0.10 ===\n\n";


    CoreThreadPool pool(2);


    pool.start();


    std::cout
        << "[MAIN] Pool started with no tasks\n";


    // -----------------------------------------------------
    // Worker에게 할 일이 없는 상태.
    //
    // v0.10 Worker는 여기서 busy-loop를 돌지 않고
    // Global Work Signal을 기다리며 Sleep한다.
    // -----------------------------------------------------
    std::this_thread::sleep_for(
        std::chrono::milliseconds(1000)
    );


    std::cout
        << "\n[MAIN] Submitting tasks\n";


    auto start =
        std::chrono::steady_clock::now();


    pool.submit(
        {1, "Compile", 1000}
    );

    pool.submit(
        {2, "Physics", 600}
    );

    pool.submit(
        {3, "AI", 800}
    );

    pool.submit(
        {4, "Audio", 400}
    );

    pool.submit(
        {5, "Render", 1200}
    );


    std::cout
        << "\n[MAIN] "
        << pool.pendingTaskCount()
        << " tasks waiting\n\n";


    pool.shutdown();

    pool.wait();


    auto end =
        std::chrono::steady_clock::now();


    auto elapsed =
        std::chrono::duration_cast<
            std::chrono::milliseconds
        >(
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