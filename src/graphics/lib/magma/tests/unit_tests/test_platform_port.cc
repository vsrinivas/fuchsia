// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <thread>

#include "gtest/gtest.h"
#include "magma_util/dlog.h"
#include "platform_port.h"
#include "platform_semaphore.h"

namespace {

class TestPort {
 public:
  static void Test() {
    std::shared_ptr<magma::PlatformPort> port(magma::PlatformPort::Create());

    std::unique_ptr<std::thread> thread;

    // Verify timeout
    thread.reset(new std::thread([port] {
      DLOG("Waiting for port");
      uint64_t key;
      EXPECT_EQ(MAGMA_STATUS_TIMED_OUT, port->Wait(&key, 100).get());
      DLOG("Port wait returned");
    }));
    thread->join();

    std::shared_ptr<magma::PlatformSemaphore> sem(magma::PlatformSemaphore::Create());

    // Verify WaitAsync/Signal/Reset then Wait flow (no autoreset when waiting on a port)
    sem->WaitAsync(port.get());
    sem->Signal();
    sem->Reset();
    thread.reset(new std::thread([port, sem] {
      DLOG("Waiting for port");
      uint64_t key;
      EXPECT_EQ(MAGMA_STATUS_OK, port->Wait(&key, 100).get());
      EXPECT_EQ(key, sem->id());
      DLOG("Port wait returned 0x%" PRIx64, sem->id());
    }));
    thread->join();

    // Verify unsignalled wait - timeout
    thread.reset(new std::thread([port] {
      DLOG("Waiting for port");
      uint64_t key;
      EXPECT_EQ(MAGMA_STATUS_TIMED_OUT, port->Wait(&key, 100).get());
      DLOG("Port wait returned");
    }));
    thread->join();

    // Verify Wait then WaitAsync/Signal/Reset
    thread.reset(new std::thread([port, sem] {
      DLOG("Waiting for semaphore");
      uint64_t key;
      EXPECT_EQ(MAGMA_STATUS_OK, port->Wait(&key).get());
      EXPECT_EQ(key, sem->id());
      DLOG("Semaphore wait returned");
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sem->WaitAsync(port.get());
    sem->Signal();
    sem->Reset();
    thread->join();

    // TODO(ZX-594) - enable: Verify WaitAsync/Wait then kill the handle
#if 0
        sem->WaitAsync(port.get());
        thread.reset(new std::thread([port] {
            DLOG("Waiting for semaphore");
            uint64_t key;
            EXPECT_EQ(MAGMA_STATUS_OK, port->Wait(&key).get());
            DLOG("Semaphore wait returned");
        }));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_EQ(1u, sem.use_count());
        sem.reset();
        thread->join();
#endif

    // Verify close
    // TODO(ZX-594): test Close after Wait also
    port->Close();
    thread.reset(new std::thread([port, sem] {
      DLOG("Waiting for semaphore");
      uint64_t key;
      EXPECT_EQ(MAGMA_STATUS_INTERNAL_ERROR, port->Wait(&key).get());
      DLOG("Semaphore wait returned");
    }));
    thread->join();
  }
};

}  // namespace

TEST(PlatformPort, Test) { TestPort::Test(); }
