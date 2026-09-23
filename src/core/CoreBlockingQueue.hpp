#pragma once

#include <cstddef>

#include "CoreConditionVariable.hpp"
#include "CoreLockGuard.hpp"
#include "CoreQueue.hpp"
#include "CoreSpinLock.hpp"
#include "CoreUniqueLock.hpp"

template <typename T>
class CoreBlockingQueue
{
private:
    CoreQueue<T> m_queue;

    CoreSpinLock m_lock;

    CoreConditionVariable m_condition;

    bool m_closed;

public:
    CoreBlockingQueue()
        : m_closed(false)
    {
    }

    CoreBlockingQueue(
        const CoreBlockingQueue&
    ) = delete;

    CoreBlockingQueue& operator=(
        const CoreBlockingQueue&
    ) = delete;

    /*
        push

        Queue에 데이터를 추가하고 
        잠들어 있는 Worker 하나를 깨운다.

        Queue가 close된 뒤에는 추가하지 않는다.
    */
    bool push(const T& value)
    {
        {
            CoreLockGuard<CoreSpinLock> guard(m_lock);

            if(m_closed)
            {
                return false;
            }

            m_queue.push(value);
        }

        // Queue Lock을 해제한 뒤 Worker를 깨운다.
        m_condition.notifyOne();

        return true;
    }

    /*
        waitPop

        Task가 없다면 Thread가 종료되지 않고 기다린다.

        다음 두 조건 중 하나가 발생하면 깨어난다.
        1. Queue에 Task가 생김
        2. Queue가 close됨
    */
    bool waitPop(T& output)
    {
        CoreUniqueLock<CoreSpinLock> lock(m_lock);

        m_condition.wait(
            lock,
            [this]()
            {
                return m_closed || !m_queue.empty();
            }
        );

        // close됐고 남은 Task도 없다.
        if (m_queue.empty())
        {
            return false;
        }

        return m_queue.tryPop(output);
    }

    /*
        close

        이제 새로운 Task가 들어오지 않는다는 의미
        잠들어 있는 Worker를 모두 깨운다.
    */
    void close()
    {
        {
            CoreLockGuard<CoreSpinLock> guard(m_lock);

            m_closed = true;
        }

        m_condition.notifyAll();
    }

    std::size_t size()
    {
        CoreLockGuard<CoreSpinLock> gurad(m_lock);

        return m_queue.size();
    }

    bool empty()
    {
        CoreLockGuard<CoreSpinLock> guard(m_lock);

        return m_queue.empty();
    }

    bool closed()
    {
        CoreLockGuard<CoreSpinLock> guard(m_lock);

        return m_closed;
    }
};