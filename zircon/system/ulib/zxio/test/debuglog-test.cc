// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/zxio.h>

#include <array>
#include <thread>
#include <utility>

#include <zxtest/zxtest.h>

namespace {

class DebugLogTest : public zxtest::Test {
 protected:
  void SetUp() override final {
    ASSERT_OK(zx::channel::create(0, &local_, &remote_));
    constexpr char kWriteOnlyLogPath[] = "/svc/" fuchsia_boot_WriteOnlyLog_Name;
    ASSERT_OK(fdio_service_connect(kWriteOnlyLogPath, remote_.release()));
    zx::debuglog handle;
    ASSERT_OK(fuchsia_boot_WriteOnlyLogGet(local_.get(), handle.reset_and_get_address()));

    storage_ = std::make_unique<zxio_storage_t>();
    ASSERT_OK(zxio_debuglog_init(storage_.get(), std::move(handle)));
    logger_ = &storage_->io;
    ASSERT_NE(logger_, nullptr);
  }

  void TearDown() override final {
    if (logger_)
      ASSERT_OK(zxio_destroy(logger_));
  }

  zx::channel local_;
  zx::channel remote_;
  std::unique_ptr<zxio_storage_t> storage_;
  zxio_t* logger_;
};

constexpr size_t kNumThreads = 256;

// Sets up multiple threads that write to debuglog concurrently; ASAN or TSAN builders will
// be responsible for catching bugs.
std::array<std::thread, kNumThreads> StartStressingThreads(zxio_t* logger,
                                                           bool allow_handle_closed_error) {
  std::array<std::thread, kNumThreads> threads;

  for (size_t i = 0; i < kNumThreads; ++i) {
    threads[i] = std::thread([i, logger, allow_handle_closed_error]() {
      std::string log_str = "output from " + std::to_string(i) + "\n";
      size_t actual;
      zx_status_t status = zxio_write(logger, log_str.c_str(), log_str.size(), 0, &actual);
      // We would get |ZX_ERR_BAD_HANDLE| if the debuglog was closed.
      if (!allow_handle_closed_error || status != ZX_ERR_BAD_HANDLE) {
        ASSERT_OK(status);
        ASSERT_EQ(actual, log_str.size());
      }
    });
  }

  return threads;
}

TEST_F(DebugLogTest, ThreadSafety) {
  auto threads = StartStressingThreads(logger_, /* allow_handle_closed_error */ false);

  for (auto& t : threads) {
    t.join();
  }
  ASSERT_OK(zxio_close(logger_));
}

TEST_F(DebugLogTest, ThreadSafety_CloseDuringWrite) {
  auto threads = StartStressingThreads(logger_, /* allow_handle_closed_error */ true);

  ASSERT_OK(zxio_close(logger_));
  for (auto& t : threads) {
    t.join();
  }
}

}  // namespace
