// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/common/cancelable_callback.h"

#include <thread>
#include <unordered_map>

#include "gtest/gtest.h"

#include "lib/fxl/synchronization/sleep.h"
#include "lib/fxl/time/stopwatch.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/threading/create_thread.h"

namespace bluetooth {
namespace common {
namespace {

TEST(CancelableCallbackTest, CancelAndRunOnSameThread) {
  CancelableCallbackFactory<void()> factory;

  EXPECT_FALSE(factory.canceled());

  bool called = false;
  auto callback = factory.MakeTask([&called] { called = true; });

  callback();
  EXPECT_TRUE(called);

  called = false;
  factory.CancelAll();
  EXPECT_TRUE(factory.canceled());

  callback();
  EXPECT_FALSE(called);
}

TEST(CancelableCallbackTest, CancelAndRunOnDifferentThreads) {
  CancelableCallbackFactory<void()> factory;
  EXPECT_FALSE(factory.canceled());

  fxl::RefPtr<fxl::TaskRunner> thrd_runner;
  auto thrd = fsl::CreateThread(&thrd_runner, "CancelableCallbackTest thread");

  bool called = false;
  auto cb = factory.MakeTask([&called] { called = true; });

  // Make sure the task is canceled before it gets run.
  factory.CancelAll();
  EXPECT_TRUE(factory.canceled());

  thrd_runner->PostTask(cb);
  thrd_runner->PostTask([] { fsl::MessageLoop::GetCurrent()->QuitNow(); });

  if (thrd.joinable()) thrd.join();

  EXPECT_FALSE(called);
}

TEST(CancelableCallbackTest, CancelAllBlocksDuringCallback) {
  constexpr int kLoopCount = 100;
  constexpr int64_t kBlockTimeMs = 10;

  for (int i = 0; i < kLoopCount; ++i) {
    CancelableCallbackFactory<void()> factory;
    EXPECT_FALSE(factory.canceled());

    std::mutex mtx;
    std::condition_variable cv;
    bool ready = false;

    auto callback = [&] {
      // This makes sure that CancelAll() is called after this.
      {
        std::lock_guard<std::mutex> lock(mtx);
        ready = true;
      }
      cv.notify_one();

      fxl::SleepFor(fxl::TimeDelta::FromMilliseconds(kBlockTimeMs));
    };

    fxl::Stopwatch sw;
    sw.Start();

    std::thread thrd(factory.MakeTask(callback));
    thrd.detach();

    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&ready] { return ready; });

    // This should block for at least |kBlockTimeMs| milliseconds as that is how long |callback|
    // will sleep for.
    factory.CancelAll();
    ASSERT_GE(sw.Elapsed().ToMilliseconds(), kBlockTimeMs);
  }
}

}  // namespace
}  // namespace common
}  // namespace bluetooth
