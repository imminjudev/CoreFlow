#pragma once

#include <cstddef>

template <typename T>
class CoreDeque
{
private:
    /*
        Doubly Linked List Node

        Queue에서는 next만 필요했지만,
        Deque는 앞/뒤 양쪽으로 이동해야 하므로
        prev와 next를 모두 가진다.
    */
    struct Node
    {
        T data;

        Node* prev;
        Node* next;

        explicit Node(const T& value)
            : data(value),
              prev(nullptr),
              next(nullptr)
        {
        }
    };

private:
    // 가장 앞 Node
    Node* m_front;

    // 가장 뒤 Node
    Node* m_back;

    // 현재 원소 개수
    std::size_t m_size;

public:
    CoreDeque()
        : m_front(nullptr),
          m_back(nullptr),
          m_size(0)
    {
    }

    ~CoreDeque()
    {
        clear();
    }

    // 포인터 기반 자료구조이므로 아직 복사는 금지한다.
    CoreDeque(const CoreDeque&) = delete;

    CoreDeque& operator=(
        const CoreDeque&
    ) = delete;

    /*
        pushBack

        Deque 뒤쪽에 삽입

        [10][20][30]

        pushBack(40)
        [10][20][30][40]
    */
    void pushBack(const T& value)
    {
        Node* newNode = new Node(value);

        // Deque가 비어 있는 경우
        if (m_back == nullptr)
        {
            m_front = newNode;
            m_back = newNode;
        }
        else
        {
            newNode->prev = m_back;

            m_back->next = newNode;

            m_back = newNode;
        }

        m_size++;
    }

    /*
        pushFront

        Deque 앞쪽에 삽입

        [10][20][30]

        pushFront(5)
        [5][10][20][30]
    */
    void pushFront(const T& value)
    {
        Node* newNode = new Node(value);

        if (m_front == nullptr)
        {
            m_front = newNode;
            m_back = newNode;
        }
        else
        {
            newNode->next = m_front;

            m_front->prev = newNode;

            m_front = newNode;
        }

        m_size++;
    }

    /*
        tryPopFront

        앞쪽 데이터를 꺼낸다.

        Work Stealing에서는 나중에
        다른 Worker가 Task를 훔칠 때 사용한다.
    */
    bool tryPopFront(T& output)
    {
        if (m_front == nullptr)
        {
            return false;
        }

        Node* oldFront = m_front;

        // 제거할 Node의 데이터를 output으로 복사
        output = oldFront->data;

        // front를 다음 Node로 이동
        m_front = oldFront->next;

        if (m_front != nullptr)
        {
            m_front->prev = nullptr;
        }
        else
        {
            // 마지막 원소까지 제거했다면 back도 nullptr
            m_back = nullptr;
        }

        delete oldFront;

        m_size--;

        return true;
    }

    /*
        tryPopBack

        뒤쪽 데이터를 꺼낸다.

        나중에 각 Worker는 자신의 Local Queue에서
        주로 이 함수를 사용한다.
    */
    bool tryPopBack(T& output)
    {
        if (m_back == nullptr)
        {
            return false;
        }

        Node* oldBack = m_back;

        output = oldBack->data;

        // back을 이전 Node로 이동
        m_back = oldBack->prev;

        if (m_back != nullptr)
        {
            m_back->next = nullptr;
        }
        else
        {
            // 마지막 원소였다면 front 역시 nullptr
            m_front = nullptr;
        }

        delete oldBack;

        m_size--;

        return true;
    }

    bool empty() const
    {
        return m_size == 0;
    }

    std::size_t size() const
    {
        return m_size;
    }

    /*
        clear

        모든 Node 제거
    */
    void clear()
    {
        T temp;

        while (tryPopFront(temp))
        {
        }
    }
};