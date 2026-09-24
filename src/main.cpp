#include <chrono>
#include <iostream>
#include <thread>

#include "runtime/CoreThreadPool.hpp"


int main()
{
    std::cout
        << "=== CoreFlow v0.11 ===\n\n";


    CoreThreadPool pool(2);


    pool.start();


    std::cout
        << "[MAIN] Pool started with no tasks\n";


    // Worker들이 일이 없는 상태에서
    // Sleep하는 상황을 만든다.
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


    // -----------------------------------------------------
    // 모든 Worker가 join된 이후 통계 출력.
    //
    // 따라서 WorkerStatistics를 Main Thread가 읽는 동안
    // 다른 Worker가 수정하지 않는다.
    // -----------------------------------------------------
    pool.printStatistics();


    return 0;
}