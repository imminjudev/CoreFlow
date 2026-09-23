#pragma once

template <typename LockType>
class CoreUniqueLock
{
private:
    LockType* m_lock;
    bool m_ownsLock;

public:
    explicit CoreUniqueLock(LockType& lock)
        : m_lock(&lock),
          m_ownsLock(false)
    {
        this->lock();
    }

    ~CoreUniqueLock()
    {
        if(m_ownsLock)
        {
            m_lock->unlock();
        }
    }

    CoreUniqueLock(const CoreUniqueLock&) = delete;
    CoreUniqueLock& operator=(const CoreUniqueLock&) = delete;

    void lock()
    {
        if(!m_ownsLock)
        {
            m_lock->lock();
            m_ownsLock = true;
        }
    }

    void unlock()
    {
        if(m_lock)
        {
            m_lock->unlock();
            m_ownsLock = false;
        }
    }

    bool ownsLock() const
    {
        return m_ownsLock;
    }
};