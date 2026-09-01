#include <gtest/gtest.h>
#include "common/thread_pool.hpp"
#include <atomic>

TEST(ThreadPoolTest, SubmitTasks) {
    dse::ThreadPool pool(4);
    std::atomic<int> counter{0};

    std::vector<std::future<void>> futures;
    for (int i = 0; i < 100; ++i) {
        futures.push_back(pool.submit([&counter]() { counter++; }));
    }
    for (auto& f : futures) f.get();
    EXPECT_EQ(counter.load(), 100);
}

TEST(ThreadPoolTest, ReturnValue) {
    dse::ThreadPool pool(2);
    auto future = pool.submit([](int a, int b) { return a + b; }, 3, 4);
    EXPECT_EQ(future.get(), 7);
}
