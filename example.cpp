#include "threadpool.h"
#include <iostream>

int sink(std::unique_ptr<int> p)
{
    return *p;
}

int main()
{
    ThreadPool<4> p;

    // works with lambda's
    auto fut = p.enqueue([](int i) -> int {return i * i;}, 4);

    // works with move only classes
    auto fut2 = p.enqueue(&sink, std::make_unique<int>(42));

    std::cout << fut.get() << '\n' << fut2.get() << '\n';
}