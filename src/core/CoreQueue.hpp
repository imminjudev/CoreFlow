#pragma once

#include <cstddef>

template <typename T>
class CoreQueue
{
private:
    struct Node
    {
        T data;
        Node* next;

        Node(const T& value)
            : data(value),
              next(nullptr)
        {
        }
    };

private:
    Node* m_front;
    Node* m_back;

    std::size_t m_size;

public:
    CoreQueue()
        : m_front(nullptr),
          m_back(nullptr),
          m_size(0)
    {
    }

    ~CoreQueue()
    {
        clear();
    }

    /*
        지금 단계에서는 Queue 복사를 금지한다.

        포인터를 포함하는 자료구조를 그냥 복사하면
        두 Queue가 같은 Node를 가리키는 문제가 발생할 수 있다.
    */
    CoreQueue(const CoreQueue&) = delete;

    CoreQueue& operator=(const CoreQueue&) = delete;

    void push(const T& value)
    {
        Node* newNode = new Node(value);

        // Queue가 비어 있는 경우
        if (m_back == nullptr)
        {
            m_front = newNode;
            m_back = newNode;
        }
        else
        {
            // 기존 마지막 Node가 새 Node를 가리키게 한다.
            m_back->next = newNode;

            // Queue의 끝을 새 Node로 이동
            m_back = newNode;
        }

        m_size++;
    }

    void pop()
    {
        if (m_front == nullptr)
        {
            return;
        }

        Node* oldFront = m_front;

        m_front = m_front->next;

        delete oldFront;

        m_size--;

        // 마지막 원소까지 제거했다면 back도 nullptr로 바꿔야 한다.
        if(m_front == nullptr)
        {
            m_back = nullptr;
        }
    }

    T& front()
    {
        return m_front->data;
    }

    const T& front() const
    {
        return m_front->data;
    }

    bool empty() const
    {
        return m_front == nullptr;
    }

    std::size_t size() const
    {
        return m_size;
    }

    void clear()
    {
        while (!empty())
        {
            pop();
        }
    }

    bool tryPop(T& output)
    {
        if(m_front == nullptr)
        {
            return false;
        }

        Node* oldFront = m_front;
        
        output = oldFront->data;

        m_front = m_front->next;

        delete oldFront;

        m_size--;

        if(m_front == nullptr)
        {
            m_back = nullptr;
        }

        return true;
    }
};