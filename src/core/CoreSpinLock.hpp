#pragma once

#include <atomic>
#include <thread>

class CoreSpinLock
{
private:
    std::atomic_flag m_flag = ATOMIC_FLAG_INIT;

public:
    CoreSpinLock() = default;

    CoreSpinLock(const CoreSpinLock&) = delete;
    CoreSpinLock& operator=(const CoreSpinLock&) = delete;

    void lock()
    {
        while (m_flag.test_and_set(std::memory_order_acquire))
        {
            // 다른 Thread가 Lock을 가지고 있다.
            // CPU를 계속 독점하지 않도록 실행 기회를 양보한다.
            std::this_thread::yield();
        }
    }

    void unlock()
    {
        m_flag.clear(std::memory_order_release);
    }
};