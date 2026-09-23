#pragma once

#include <cstddef>

#include "CoreConditionVariable.hpp"
#include "CoreDeque.hpp"
#include "CoreLockGuard.hpp"
#include "CoreSpinLock.hpp"
#include "CoreUniqueLock.hpp"

template <typename T>
class CoreBlockingDeque
{
private:
    CoreDeque<T> m_deque;

    CoreSpinLock m_lock;

    CoreConditionVariable m_condition;

    bool m_closed;

public:
    CoreBlockingDeque()
        : m_closed(false)
    {
    }

    CoreBlockingDeque(
        const CoreBlockingDeque&
    ) = delete;

    CoreBlockingDeque& operator=(
        const CoreBlockingDeque&
    ) = delete;

    /*
        Owner 쪽에 Task 삽입

        Work Stealing 구조에서는 Worker 자신의 작업은
        Deque 뒤쪽에서 관리한다.
    */
    bool pushBack(const T& value)
    {
        {
            CoreLockGuard<CoreSpinLock> guard(m_lock);

            if (m_closed)
            {
                return false;
            }

            m_deque.pushBack(value);
        }

        m_condition.notifyOne();

        return true;
    }

    /*
        자신의 Local Queue에서 작업 가져오기

        Task가 없다면 잠들어서 기다린다.

        자신의 Task는 뒤쪽에서 가져온다.
    */
    bool waitPopBack(T& output)
    {
        CoreUniqueLock<CoreSpinLock> lock(m_lock);

        m_condition.wait(
            lock,
            [this]()
            {
                return m_closed || !m_deque.empty();
            }
        );

        if (m_deque.empty())
        {
            return false;
        }

        return m_deque.tryPopBack(output);
    }

    /*
        다른 Worker가 Task를 훔칠 때 사용할 함수

        아직 v0.8에서는 사용하지 않는다.

        v0.9에서 steal에 사용한다.
    */
    bool tryPopFront(T& output)
    {
        CoreLockGuard<CoreSpinLock> guard(m_lock);

        return m_deque.tryPopFront(output);
    }

    bool tryPopBack(T& output)
    {
        CoreLockGuard<CoreSpinLock> guard(m_lock);

        return m_deque.tryPopBack(output);
    }

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
        CoreLockGuard<CoreSpinLock> guard(m_lock);

        return m_deque.size();
    }

    bool empty()
    {
        CoreLockGuard<CoreSpinLock> guard(m_lock);

        return m_deque.empty();
    }
};