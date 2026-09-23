#include <chrono>
#include <iostream>

#include "runtime/CoreThreadPool.hpp"


int main()
{
    std::cout
        << "=== CoreFlow v0.5 ===\n\n";


    // -----------------------------------------------------
    // Worker 2개를 가진 Thread Pool 생성
    // -----------------------------------------------------
    CoreThreadPool pool(2);


    // -----------------------------------------------------
    // Task 제출
    //
    // 아직 v0.5에서는 Worker 시작 전에
    // 모든 Task를 Queue에 넣는다.
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
        << "[MAIN] "
        << pool.pendingTaskCount()
        << " tasks submitted\n\n";


    // -----------------------------------------------------
    // 실행 시간 측정 시작
    // -----------------------------------------------------
    auto start =
        std::chrono::steady_clock::now();


    // Worker Thread 시작
    pool.start();


    // 모든 Worker 종료 대기
    pool.wait();


    // -----------------------------------------------------
    // 실행 시간 측정 종료
    // -----------------------------------------------------
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