// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/dlog.h"
#include "magma_util/semaphore_port.h"
#include "gtest/gtest.h"
#include <chrono>
#include <thread>

namespace {

class TestSemaphorePort {
public:
    void Test(uint32_t semaphore_count)
    {
        auto semaphore_port = std::shared_ptr<magma::SemaphorePort>(magma::SemaphorePort::Create());

        std::vector<std::shared_ptr<magma::PlatformSemaphore>> semaphores;
        for (uint32_t i = 0; i < semaphore_count; i++) {
            semaphores.push_back(magma::PlatformSemaphore::Create());
        }

        // Makes a copy of the semaphores vector
        auto wait_set = std::make_unique<magma::SemaphorePort::WaitSet>(
            [this](magma::SemaphorePort::WaitSet* wait_set) { ++this->callback_count_; },
            semaphores);

        EXPECT_TRUE(semaphore_port->AddWaitSet(std::move(wait_set)));

        for (uint32_t i = 0; i < semaphore_count; i++) {
            EXPECT_EQ(2u, semaphores[i].use_count());
        }

        EXPECT_EQ(0u, callback_count_);

        std::thread thread([semaphore_port] {
            DLOG("Wait thread starting");
            while (semaphore_port->WaitOne())
                ;
            DLOG("WaitOne returned false, thread exiting");
        });

        auto copy = semaphores;

        for (uint32_t i = 0; i < semaphore_count; i++) {
            uint32_t index = rand() % semaphores.size();
            DLOG("signalling semaphore 0x%" PRIx64, semaphores[index]->id());
            semaphores[index]->Signal();
            semaphores.erase(semaphores.begin() + index);
            std::this_thread::yield();
        }

        auto start = std::chrono::high_resolution_clock::now();
        while (callback_count_ < 1 && std::chrono::duration<double, std::milli>(
                                          std::chrono::high_resolution_clock::now() - start)
                                              .count() < 1000)
            std::this_thread::yield();

        DLOG("closing semaphore port");
        semaphore_port->Close();

        thread.join();

        EXPECT_EQ(1u, callback_count_);

        semaphores = std::move(copy);

        for (uint32_t i = 0; i < semaphore_count; i++) {
            EXPECT_EQ(1u, semaphores[i].use_count());
            // Semaphores should be unsignalled
            EXPECT_FALSE(semaphores[i]->Wait(10));
        }
    }

private:
    volatile uint32_t callback_count_ = 0;
};

} // namespace

TEST(SemaphorePort, One) { std::make_unique<TestSemaphorePort>()->Test(1); }

TEST(SemaphorePort, Many) { std::make_unique<TestSemaphorePort>()->Test(50); }
