#pragma once

template <typename LockType>
class CoreLockGuard
{
private:
    LockType& m_lock;

public:
    explicit CoreLockGuard(LockType& lock)
        : m_lock(lock)
    {
        m_lock.lock();
    }

    ~CoreLockGuard()
    {
        m_lock.unlock();
    }

    CoreLockGuard(const CoreLockGuard&) = delete;
    CoreLockGuard& operator=(const CoreLockGuard&) = delete;
};