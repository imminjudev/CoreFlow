#include <chrono>
#include <iostream>

#include "runtime/CoreThreadPool.hpp"


int main()
{
    std::cout
        << "=== CoreFlow v0.8 ===\n\n";


    CoreThreadPool pool(2);


    // Worker 먼저 실행
    pool.start();


    auto start =
        std::chrono::steady_clock::now();


    // -----------------------------------------------------
    // Round-Robin으로 분배된다.
    //
    // Worker 0:
    // T1 = 1000
    // T3 = 800
    // T5 = 1200
    //
    // 총 3000ms
    //
    // Worker 1:
    // T2 = 600
    // T4 = 400
    //
    // 총 1000ms
    //
    // Work Stealing이 없기 때문에
    // Worker 1이 먼저 놀게 될 것이다.
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


    // 새 Task 제출 종료
    pool.shutdown();


    // 기존 Task 처리 완료까지 기다림
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