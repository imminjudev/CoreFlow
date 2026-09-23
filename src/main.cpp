#include <chrono>
#include <iostream>

#include "runtime/CoreThreadPool.hpp"


int main()
{
    std::cout
        << "=== CoreFlow v0.9 ===\n\n";


    CoreThreadPool pool(2);


    pool.start();


    auto start =
        std::chrono::steady_clock::now();


    // -----------------------------------------------------
    // Round-Robin 초기 배치
    //
    // Worker 0
    // T1 = 1000
    // T3 = 800
    // T5 = 1200
    //
    // Worker 1
    // T2 = 600
    // T4 = 400
    //
    // 초기 상태는 의도적으로 불균형하다.
    // -----------------------------------------------------

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