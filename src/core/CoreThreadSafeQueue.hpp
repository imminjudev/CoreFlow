#pragma once

#include <cstddef>

#include "CoreLockGuard.hpp"
#include "CoreQueue.hpp"
#include "CoreSpinLock.hpp"

template <typename T>
class CoreThreadSafeQueue
{
private:
    CoreQueue<T> m_queue;
    CoreSpinLock m_lock;

public:
    CoreThreadSafeQueue() = default;

    CoreThreadSafeQueue(const CoreThreadSafeQueue&) = delete;
    CoreThreadSafeQueue& operator=(const CoreThreadSafeQueue&) = delete;

    void push(const T& value)
    {
        CoreLockGuard<CoreSpinLock> guard(m_lock);

        m_queue.push(value);
    }

    bool tryPop(T& output)
    {
        CoreLockGuard<CoreSpinLock> guard(m_lock);

        return m_queue.tryPop(output);
    }

    bool empty()
    {
        CoreLockGuard<CoreSpinLock> guard(m_lock);

        return m_queue.empty();
    }

    std::size_t size()
    {
        CoreLockGuard<CoreSpinLock> guard(m_lock);

        return m_queue.size();
    }
};