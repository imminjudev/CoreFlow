#pragma once

#include <condition_variable>

class CoreConditionVariable
{
private:
    std::condition_variable_any m_condition;

public:
    CoreConditionVariable() = default;

    CoreConditionVariable(const CoreConditionVariable&) = delete;
    CoreConditionVariable& operator=(const CoreConditionVariable&) = delete;

    template <typename LockType, typename Predicate>
    void wait(
        LockType& lock,
        Predicate predicate
    )
    {
        m_condition.wait(
            lock,
            predicate
        );
    }

    void notifyOne()
    {
        m_condition.notify_one();
    }

    void notifyAll()
    {
        m_condition.notify_all();
    }
};