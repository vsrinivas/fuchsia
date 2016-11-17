// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/monitor.h"
#include "gtest/gtest.h"
#include <chrono>
#include <thread>

class TestMonitor {
public:
    static void first_thread_entry(std::shared_ptr<magma::Monitor> monitor)
    {
        magma::Monitor::Lock lock(monitor);
        lock.Acquire();
        while (!signalled)
            monitor->Wait(&lock);
        signalled = false;
        lock.Release();

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        signalled = true;
        monitor->Signal();
    }

    static void second_thread_entry(std::shared_ptr<magma::Monitor> monitor)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        signalled = true;
        monitor->Signal();

        magma::Monitor::Lock lock(monitor);
        lock.Acquire();
        while (!signalled)
            monitor->Wait(&lock);
        signalled = false;
        lock.Release();
    }

    static void Test()
    {
        bool timed_out = false;
        auto monitor = magma::Monitor::CreateShared();

        magma::Monitor::Lock lock(monitor);
        lock.Acquire();
        monitor->WaitUntil(&lock, std::chrono::high_resolution_clock::now() +
                                      std::chrono::milliseconds(10),
                           &timed_out);
        lock.Release();
        EXPECT_TRUE(timed_out);

        std::thread first(first_thread_entry, monitor);
        std::thread second(second_thread_entry, monitor);

        first.join();
        second.join();
    }

    static bool signalled;
};

bool TestMonitor::signalled;

TEST(Monitor, Test) { TestMonitor::Test(); }
