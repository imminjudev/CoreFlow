#pragma once

#include <cstddef>

#include "CoreConditionVariable.hpp"
#include "CoreLockGuard.hpp"
#include "CoreSpinLock.hpp"
#include "CoreUniqueLock.hpp"


// ---------------------------------------------------------
// CoreStartBarrier
//
// 여러 Worker를 하나의 시작 지점에서 대기시킨다.
//
// Worker:
//     arriveAndWait()
//
// Main:
//     waitUntilReady()
//     release()
//
// 모든 Worker가 Barrier에 도착한 뒤
// Main이 release()를 호출해야 실행을 시작한다.
// ---------------------------------------------------------
class CoreStartBarrier
{
private:
    CoreSpinLock m_lock;

    CoreConditionVariable m_condition;

    // Barrier에 도착해야 하는 Worker 수
    std::size_t m_targetCount;

    // 현재 Barrier에 도착한 Worker 수
    std::size_t m_arrivedCount;

    // Main이 실행을 허용했는가
    bool m_released;


public:
    explicit CoreStartBarrier(
        std::size_t targetCount
    )
        : m_targetCount(targetCount),
          m_arrivedCount(0),
          m_released(false)
    {
    }


    CoreStartBarrier(
        const CoreStartBarrier&
    ) = delete;


    CoreStartBarrier& operator=(
        const CoreStartBarrier&
    ) = delete;


    // -----------------------------------------------------
    // Worker가 Barrier에 도착했을 때 호출한다.
    //
    // 자신의 도착을 기록한 뒤 release()될 때까지 Sleep.
    // -----------------------------------------------------
    void arriveAndWait()
    {
        CoreUniqueLock<CoreSpinLock> lock(m_lock);


        m_arrivedCount++;


        // Main이 waitUntilReady()에서 기다리고 있을 수 있으므로
        // 도착 상태가 변경됐음을 알려준다.
        m_condition.notifyAll();


        // Main이 Barrier를 열 때까지 대기
        m_condition.wait(
            lock,
            [this]()
            {
                return m_released;
            }
        );
    }


    // -----------------------------------------------------
    // Main Thread에서 호출.
    //
    // 모든 Worker가 Barrier에 도착할 때까지 기다린다.
    // -----------------------------------------------------
    void waitUntilReady()
    {
        CoreUniqueLock<CoreSpinLock> lock(m_lock);


        m_condition.wait(
            lock,
            [this]()
            {
                return
                    m_arrivedCount
                    >= m_targetCount;
            }
        );
    }


    // -----------------------------------------------------
    // Barrier 개방.
    //
    // 기다리고 있는 모든 Worker를 동시에 깨운다.
    // -----------------------------------------------------
    void release()
    {
        {
            CoreLockGuard<CoreSpinLock> guard(m_lock);


            if (m_released)
            {
                return;
            }


            m_released = true;
        }


        m_condition.notifyAll();
    }
};