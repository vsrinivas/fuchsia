// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/wakeup_event.h"

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace media::audio {
namespace {

class WakeupEventTest : public gtest::TestLoopFixture {
 protected:
  zx_status_t process_handler(WakeupEvent* e) {
    process_count_++;
    return process_result_;
  }
  uint32_t process_count() const { return process_count_; }

  void set_process_result(zx_status_t status) { process_result_ = status; }

 private:
  zx_status_t process_result_ = ZX_OK;
  uint32_t process_count_ = 0;
};

TEST_F(WakeupEventTest, Signal) {
  WakeupEvent event;

  EXPECT_EQ(ZX_OK, event.Activate(dispatcher(), [this](WakeupEvent* e) -> zx_status_t {
    return process_handler(e);
  }));

  RunLoopUntilIdle();
  EXPECT_EQ(0u, process_count());

  EXPECT_EQ(ZX_OK, event.Signal());
  RunLoopUntilIdle();
  EXPECT_EQ(1u, process_count());

  EXPECT_EQ(ZX_OK, event.Signal());
  RunLoopUntilIdle();
  EXPECT_EQ(2u, process_count());
}

TEST_F(WakeupEventTest, SignalFromHandler) {
  WakeupEvent event;

  EXPECT_EQ(ZX_OK, event.Activate(dispatcher(), [this, &event](WakeupEvent* e) -> zx_status_t {
    if (process_count() == 0) {
      EXPECT_EQ(ZX_OK, event.Signal());
    }
    return process_handler(e);
  }));

  RunLoopUntilIdle();
  EXPECT_EQ(0u, process_count());

  // We signal once here and once the first time the handler is called. Hence we expect 2
  // invocations here now.
  EXPECT_EQ(ZX_OK, event.Signal());
  RunLoopUntilIdle();
  EXPECT_EQ(2u, process_count());

  // One more |Signal| (the handler will not be signalling this time).
  EXPECT_EQ(ZX_OK, event.Signal());
  RunLoopUntilIdle();
  EXPECT_EQ(3u, process_count());
}

TEST_F(WakeupEventTest, StopWaitingWhenHandlerFails) {
  WakeupEvent event;

  EXPECT_EQ(ZX_OK, event.Activate(dispatcher(), [this](WakeupEvent* e) -> zx_status_t {
    return process_handler(e);
  }));

  RunLoopUntilIdle();
  EXPECT_EQ(0u, process_count());

  EXPECT_EQ(ZX_OK, event.Signal());
  RunLoopUntilIdle();
  EXPECT_EQ(1u, process_count());

  // Set the handler to fail, we should get one more invocation.
  set_process_result(ZX_ERR_SHOULD_WAIT);
  EXPECT_EQ(ZX_OK, event.Signal());
  RunLoopUntilIdle();
  EXPECT_EQ(2u, process_count());

  // Now if we signal again we should see no further interactions.
  EXPECT_EQ(ZX_OK, event.Signal());
  RunLoopUntilIdle();
  EXPECT_EQ(2u, process_count());
}

}  // namespace
}  // namespace media::audio
