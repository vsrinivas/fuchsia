// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/zx/clock.h>
#include <lib/zx/event.h>

#include "src/media/vnext/lib/threads/thread.h"

namespace fmlib::test {

class ThreadTest : public gtest::RealLoopFixture {
 protected:
  // Indicates that |RunLoopUntilDoneLooping| should stop looping.
  void DoneLooping() {
    async::PostTask(dispatcher(), [this]() { done_looping_ = true; });
  }

  // Determines whether |DoneLooping| has been called since the last call to
  // |RunLoopUntilDoneLooping|.
  bool is_done_looping() const { return done_looping_; }

  // Runs the loop until |DoneLooping| is called in a task or other thread.
  void RunLoopUntilDoneLooping() {
    RunLoopUntil([this]() {
      if (!done_looping_) {
        return false;
      }

      done_looping_ = false;
      return true;
    });
  }

 private:
  bool done_looping_ = false;
};

// Tests a |Thread| created with |Thread::CreateNewThread|.
TEST_F(ThreadTest, NewThread) {
  Thread under_test = Thread::CreateNewThread("NewThread unit test");
  EXPECT_FALSE(under_test.is_current());

  // test loop()
  async::PostTask(under_test.loop().dispatcher(), [this, under_test]() {
    EXPECT_TRUE(under_test.is_current());
    DoneLooping();
  });
  RunLoopUntilDoneLooping();

  // test executor()
  under_test.executor().schedule_task(fpromise::make_promise<>([this, under_test]() {
    EXPECT_TRUE(under_test.is_current());
    DoneLooping();
  }));
  RunLoopUntilDoneLooping();

  // test dispatcher()
  async::PostTask(under_test.dispatcher(), [this, under_test]() {
    EXPECT_TRUE(under_test.is_current());
    DoneLooping();
  });
  RunLoopUntilDoneLooping();

  // test PostTask()
  under_test.PostTask([this, under_test]() {
    EXPECT_TRUE(under_test.is_current());
    DoneLooping();
  });
  RunLoopUntilDoneLooping();

  // test PostTaskForTime()
  under_test.PostTaskForTime(
      [this, under_test]() {
        EXPECT_TRUE(under_test.is_current());
        DoneLooping();
      },
      zx::clock::get_monotonic());
  RunLoopUntilDoneLooping();

  // test PostDelayedTask()
  under_test.PostDelayedTask(
      [this, under_test]() {
        EXPECT_TRUE(under_test.is_current());
        DoneLooping();
      },
      zx::duration());
  RunLoopUntilDoneLooping();

  // test schedule_task()
  under_test.schedule_task(fpromise::make_promise<>([this, under_test]() {
    EXPECT_TRUE(under_test.is_current());
    DoneLooping();
  }));
  RunLoopUntilDoneLooping();

  // test MakeDelayedPromise()
  under_test.schedule_task(
      under_test.MakeDelayedPromise(zx::duration()).and_then([this, under_test]() {
        EXPECT_TRUE(under_test.is_current());
        DoneLooping();
      }));
  RunLoopUntilDoneLooping();

  // test MakePromiseForTime()
  under_test.schedule_task(
      under_test.MakePromiseForTime(zx::clock::get_monotonic()).and_then([this, under_test]() {
        EXPECT_TRUE(under_test.is_current());
        DoneLooping();
      }));
  RunLoopUntilDoneLooping();

  // test MakePromiseWaitHandle()
  zx::event test_event;
  zx_status_t status = zx::event::create(0, &test_event);
  EXPECT_EQ(status, ZX_OK);

  auto unowned_test_event = zx::unowned_handle(test_event.get());

  under_test.schedule_task(
      under_test.MakePromiseWaitHandle(std::move(unowned_test_event), ZX_EVENT_SIGNALED, 0)
          .then(
              [this, under_test](const fpromise::result<zx_packet_signal_t, zx_status_t>& result) {
                EXPECT_TRUE(result.is_ok());
                EXPECT_EQ(result.value().trigger, ZX_EVENT_SIGNALED);
                EXPECT_EQ(result.value().observed, ZX_EVENT_SIGNALED);
                EXPECT_TRUE(under_test.is_current());
                DoneLooping();
              }));
  RunLoopUntilIdle();
  EXPECT_FALSE(is_done_looping());
  test_event.signal(0, ZX_EVENT_SIGNALED);
  RunLoopUntilDoneLooping();
}

// Tests a |Thread| created with |Thread::CreateForLoop|.
TEST_F(ThreadTest, ForLoop) {
  Thread under_test = Thread::CreateForLoop(loop());
  EXPECT_TRUE(under_test.is_current());

  // test loop()
  async::PostTask(under_test.loop().dispatcher(), [this, under_test]() {
    EXPECT_TRUE(under_test.is_current());
    DoneLooping();
  });
  RunLoopUntilDoneLooping();

  // test executor()
  under_test.executor().schedule_task(fpromise::make_promise<>([this, under_test]() {
    EXPECT_TRUE(under_test.is_current());
    DoneLooping();
  }));
  RunLoopUntilDoneLooping();

  // test dispatcher()
  async::PostTask(under_test.dispatcher(), [this, under_test]() {
    EXPECT_TRUE(under_test.is_current());
    DoneLooping();
  });
  RunLoopUntilDoneLooping();

  // test PostTask()
  under_test.PostTask([this, under_test]() {
    EXPECT_TRUE(under_test.is_current());
    DoneLooping();
  });
  RunLoopUntilDoneLooping();

  // test PostTaskForTime()
  under_test.PostTaskForTime(
      [this, under_test]() {
        EXPECT_TRUE(under_test.is_current());
        DoneLooping();
      },
      zx::clock::get_monotonic());
  RunLoopUntilDoneLooping();

  // test PostDelayedTask()
  under_test.PostDelayedTask(
      [this, under_test]() {
        EXPECT_TRUE(under_test.is_current());
        DoneLooping();
      },
      zx::duration());
  RunLoopUntilDoneLooping();

  // test schedule_task()
  under_test.schedule_task(fpromise::make_promise<>([this, under_test]() {
    EXPECT_TRUE(under_test.is_current());
    DoneLooping();
  }));
  RunLoopUntilDoneLooping();

  // test MakeDelayedPromise()
  under_test.schedule_task(
      under_test.MakeDelayedPromise(zx::duration()).and_then([this, under_test]() {
        EXPECT_TRUE(under_test.is_current());
        DoneLooping();
      }));
  RunLoopUntilDoneLooping();

  // test MakePromiseForTime()
  under_test.schedule_task(
      under_test.MakePromiseForTime(zx::clock::get_monotonic()).and_then([this, under_test]() {
        EXPECT_TRUE(under_test.is_current());
        DoneLooping();
      }));
  RunLoopUntilDoneLooping();

  // test MakePromiseWaitHandle()
  zx::event test_event;
  zx_status_t status = zx::event::create(0, &test_event);
  EXPECT_EQ(status, ZX_OK);

  auto unowned_test_event = zx::unowned_handle(test_event.get());

  under_test.schedule_task(
      under_test.MakePromiseWaitHandle(std::move(unowned_test_event), ZX_EVENT_SIGNALED, 0)
          .then(
              [this, under_test](const fpromise::result<zx_packet_signal_t, zx_status_t>& result) {
                EXPECT_TRUE(result.is_ok());
                EXPECT_EQ(result.value().trigger, ZX_EVENT_SIGNALED);
                EXPECT_EQ(result.value().observed, ZX_EVENT_SIGNALED);
                EXPECT_TRUE(under_test.is_current());
                DoneLooping();
              }));
  RunLoopUntilIdle();
  EXPECT_FALSE(is_done_looping());
  test_event.signal(0, ZX_EVENT_SIGNALED);
  RunLoopUntilDoneLooping();
}

}  // namespace fmlib::test
