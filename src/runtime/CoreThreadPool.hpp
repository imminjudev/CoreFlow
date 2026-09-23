#pragma once

#include <cstddef>
#include <thread>

#include "core/CoreThreadSafeQueue.hpp"
#include "runtime/Task.hpp"

class CoreThreadPool
{
private:
    /*
        Worker Thread 배열

        std::vector<std::thread>를 사용하지 않고
        직접 동적으로 할당해서 관리한다.
    */
   std::thread* m_workers;

   // Worker 개수
   std::size_t m_workerCount;

   // 모든 Worker가 공유하는 Task Queue
   CoreThreadSafeQueue<Task> m_taskQueue;

   // start()가 호출되었는지 확인
   bool m_started;

private:
    // 각 Worker Thread가 실행할 목표
    void workerLoop(std::size_t workerId);

    // 실제 Task 실행
    void executeTask(
        std::size_t workerId,
        const Task& task
    );

public:
    explicit CoreThreadPool(
        std::size_t workerCount
    );

    ~CoreThreadPool();

    /*
        ThreadPool 자체를 복사하는 것은 금지한다.

        내부에 std::thread와 Queue 같은
        복사가 위험한 자원이 있기 때문이다.
    */
    CoreThreadPool(
        const CoreThreadPool&
    ) = delete;

    CoreThreadPool& operator=(
        const CoreThreadPool&
    ) = delete;

    // Task 등록
    void submit(const Task& task);

    // Worker Thread 생성 및 실행
    void start();
    
    // 모든 Worker 종료까지 대기
    void wait();

    // 현재 Queue에 남은 Task 개수
    std::size_t pendingTaskCount();
};