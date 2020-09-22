// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "magma_util/dlog.h"
#include "platform_port.h"
#include "platform_semaphore.h"

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#endif

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

    // TODO(fxbug.dev/30552) - enable: Verify WaitAsync/Wait then kill the handle
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
    // TODO(fxbug.dev/30552): test Close after Wait also
    port->Close();
    thread.reset(new std::thread([port, sem] {
      DLOG("Waiting for semaphore");
      uint64_t key;
      EXPECT_EQ(MAGMA_STATUS_INTERNAL_ERROR, port->Wait(&key).get());
      DLOG("Semaphore wait returned");
    }));
    thread->join();
  }

  static void TestHandle() {
#ifdef __Fuchsia__
    zx::channel local, remote;
    ASSERT_EQ(ZX_OK, zx::channel::create(0 /*flags*/, &local, &remote));

    auto handle = magma::PlatformHandle::Create(local.release());
    ASSERT_TRUE(handle);

    auto port = magma::PlatformPort::Create();
    ASSERT_TRUE(port);

    uint64_t handle_key;
    EXPECT_TRUE(handle->WaitAsync(port.get(), &handle_key));

    uint64_t key;
    EXPECT_EQ(MAGMA_STATUS_TIMED_OUT, port->Wait(&key, 0).get());

    uint32_t dummy;
    EXPECT_EQ(ZX_OK, remote.write(0 /* flags */, &dummy, sizeof(dummy), nullptr /* handles */,
                                  0 /* num_handles*/));

    // Close the peer
    remote.reset();

    EXPECT_EQ(MAGMA_STATUS_OK, port->Wait(&key, 0).get());
    EXPECT_EQ(handle_key, key);

    local.reset(handle->release());

    uint32_t actual_bytes;
    EXPECT_EQ(ZX_OK, local.read(0 /* flags */, &dummy, nullptr /*handles*/, sizeof(dummy),
                                0 /*num_handles*/, &actual_bytes, nullptr /*actual_handles*/));

    handle = magma::PlatformHandle::Create(local.release());

    EXPECT_TRUE(handle->WaitAsync(port.get(), &handle_key));

    EXPECT_EQ(MAGMA_STATUS_CONNECTION_LOST, port->Wait(&key, 0).get());
#else
    GTEST_SKIP();
#endif
  }
};

}  // namespace

TEST(PlatformPort, Test) { TestPort::Test(); }

TEST(PlatformPort, Handle) { TestPort::TestHandle(); }
