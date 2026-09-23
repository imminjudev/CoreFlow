#pragma once

#include <cstddef>

#include "CoreConditionVariable.hpp"
#include "CoreLockGuard.hpp"
#include "CoreSpinLock.hpp"
#include "CoreUniqueLock.hpp"


class CoreWorkSignal
{
private:
    CoreSpinLock m_lock;

    CoreConditionVariable m_condition;

    // 새로운 작업/상태 변화가 발생할 때마다 증가한다.
    //
    // Worker는 자신이 마지막으로 본 generation과
    // 현재 generation을 비교해서
    // 잠자는 동안 신호를 놓쳤는지 확인할 수 있다.
    std::size_t m_generation;


public:
    CoreWorkSignal()
        : m_generation(0)
    {
    }


    CoreWorkSignal(
        const CoreWorkSignal&
    ) = delete;

    CoreWorkSignal& operator=(
        const CoreWorkSignal&
    ) = delete;


    // -----------------------------------------------------
    // 현재 generation 저장
    //
    // Worker는 작업을 찾기 전에 이 값을 기억한다.
    // -----------------------------------------------------
    std::size_t snapshot()
    {
        CoreLockGuard<CoreSpinLock> guard(m_lock);

        return m_generation;
    }


    // -----------------------------------------------------
    // Worker 하나 깨우기
    //
    // 새로운 Task가 submit될 때 사용한다.
    // -----------------------------------------------------
    void notifyOne()
    {
        {
            CoreLockGuard<CoreSpinLock> guard(m_lock);

            m_generation++;
        }

        m_condition.notifyOne();
    }


    // -----------------------------------------------------
    // 모든 Worker 깨우기
    //
    // shutdown이나 전체 작업 완료 시 사용한다.
    // -----------------------------------------------------
    void notifyAll()
    {
        {
            CoreLockGuard<CoreSpinLock> guard(m_lock);

            m_generation++;
        }

        m_condition.notifyAll();
    }


    // -----------------------------------------------------
    // generation이 변하거나
    // stopPredicate가 true가 될 때까지 Sleep.
    // -----------------------------------------------------
    template <
        typename StopPredicate
    >
    void waitForChange(
        std::size_t observedGeneration,
        StopPredicate stopPredicate
    )
    {
        CoreUniqueLock<CoreSpinLock> lock(m_lock);


        m_condition.wait(
            lock,
            [this,
             observedGeneration,
             &stopPredicate]()
            {
                return
                    m_generation
                        != observedGeneration
                    ||
                    stopPredicate();
            }
        );
    }
};