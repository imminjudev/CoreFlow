#include <iostream>

#include "core/CoreDeque.hpp"


int main()
{
    std::cout
        << "=== CoreDeque Test ===\n\n";


    CoreDeque<int> deque;


    deque.pushBack(10);
    deque.pushBack(20);
    deque.pushBack(30);


    std::cout
        << "Size: "
        << deque.size()
        << '\n';


    int value;


    // 뒤에서 꺼냄
    if (deque.tryPopBack(value))
    {
        std::cout
            << "Pop Back: "
            << value
            << '\n';
    }


    // 앞에서 꺼냄
    if (deque.tryPopFront(value))
    {
        std::cout
            << "Pop Front: "
            << value
            << '\n';
    }


    deque.pushFront(5);


    if (deque.tryPopFront(value))
    {
        std::cout
            << "Pop Front: "
            << value
            << '\n';
    }


    std::cout
        << "Remaining Size: "
        << deque.size()
        << '\n';


    if (deque.tryPopBack(value))
    {
        std::cout
            << "Pop Back: "
            << value
            << '\n';
    }


    std::cout
        << "Final Size: "
        << deque.size()
        << '\n';


    return 0;
}