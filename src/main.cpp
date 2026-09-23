#include <chrono>
#include <iostream>
#include <thread>

#include "runtime/CoreThreadPool.hpp"


int main()
{
    std::cout
        << "=== CoreFlow v0.6 ===\n\n";


    CoreThreadPool pool(2);


    // -----------------------------------------------------
    // Worker를 먼저 실행한다.
    //
    // 아직 Task가 하나도 없으므로
    // 두 Worker 모두 Blocking Queue에서 잠든다.
    // -----------------------------------------------------
    pool.start();


    std::cout
        << "[MAIN] Thread pool started\n";


    // Worker가 실제로 기다리는 모습을 보기 위해
    // 잠깐 기다린다.
    std::this_thread::sleep_for(
        std::chrono::milliseconds(500)
    );


    std::cout
        << "\n[MAIN] Submitting first tasks\n";


    pool.submit(
        {1, "Compile", 1000}
    );

    pool.submit(
        {2, "Physics", 600}
    );


    // 실행 도중 새 Task가 들어오는 상황
    std::this_thread::sleep_for(
        std::chrono::milliseconds(500)
    );


    std::cout
        << "\n[MAIN] Submitting more tasks\n";


    pool.submit(
        {3, "AI", 800}
    );

    pool.submit(
        {4, "Audio", 400}
    );

    pool.submit(
        {5, "Render", 1200}
    );


    // -----------------------------------------------------
    // 이제 더 이상 Task를 제출하지 않는다.
    //
    // Queue를 닫는다.
    //
    // 기존 Queue에 남은 Task는 전부 처리한다.
    // -----------------------------------------------------
    std::cout
        << "\n[MAIN] Shutting down pool\n";

    pool.shutdown();


    // Worker가 남은 Task를 모두 끝낼 때까지 기다린다.
    pool.wait();


    std::cout
        << "\nAll tasks completed.\n";


    return 0;
}