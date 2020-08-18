// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/clock.h>
#include <lib/zx/event.h>
#include <lib/zx/object.h>
#include <lib/zx/thread.h>
#include <zircon/types.h>

#include <iterator>
#include <thread>

#include <fbl/algorithm.h>
#include <zxtest/zxtest.h>

namespace {
constexpr zx::duration kPollingInterval = zx::msec(1);
// Wait, possibly forever, until |thread| has entered |state|.
zx_status_t WaitForState(const zx::unowned_thread& thread, zx_thread_state_t state) {
  while (true) {
    zx_info_thread_t info;
    zx_status_t status = thread->get_info(ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr);
    if (status != ZX_OK) {
      return status;
    }
    if (info.state == state) {
      return ZX_OK;
    }
    zx::nanosleep(zx::deadline_after(kPollingInterval));
  }
}

zx::event MakeEvent() {
  zx::event ev;
  zx::event::create(0u, &ev);
  return ev;
}

TEST(ObjectWaitOneTest, WaitForEventSignaled) {
  auto ev = MakeEvent();
  ev.signal(0u, ZX_EVENT_SIGNALED);
  zx_signals_t observed;
  ASSERT_OK(ev.wait_one(ZX_EVENT_SIGNALED, zx::time::infinite(), &observed));
  ASSERT_EQ(observed, ZX_EVENT_SIGNALED);
}

TEST(ObjectWaitOneTest, WaitForEventTimeout) {
  auto ev = MakeEvent();
  zx_signals_t observed;
  ASSERT_EQ(ev.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(zx::msec(1)), &observed),
            ZX_ERR_TIMED_OUT);
  ASSERT_EQ(observed, 0u);
}

TEST(ObjectWaitOneTest, EmptySignalSet) {
  auto ev = MakeEvent();
  zx_signals_t observed;
  ASSERT_EQ(ev.wait_one(/*signals=*/0u, zx::deadline_after(zx::msec(1)), &observed),
            ZX_ERR_TIMED_OUT);
  ASSERT_EQ(observed, 0u);
}

TEST(ObjectWaitOneTest, WaitForEventTimeoutPreSignalClear) {
  auto ev = MakeEvent();
  ev.signal(0u, ZX_EVENT_SIGNALED);
  ev.signal(ZX_EVENT_SIGNALED, 0u);
  zx_signals_t observed;
  ASSERT_EQ(ev.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(zx::msec(1)), &observed),
            ZX_ERR_TIMED_OUT);
  ASSERT_EQ(observed, 0u);
}

TEST(ObjectWaitOneTest, WaitForEventThenSignal) {
  auto ev = MakeEvent();
  auto main_thread = zx::thread::self();
  std::thread thread([&] {
    ASSERT_OK(WaitForState(main_thread, ZX_THREAD_STATE_BLOCKED_WAIT_ONE));
    ev.signal(0u, ZX_EVENT_SIGNALED);
  });
  zx_signals_t observed;
  ASSERT_OK(ev.wait_one(ZX_EVENT_SIGNALED, zx::time::infinite(), &observed));
  ASSERT_EQ(observed, ZX_EVENT_SIGNALED);
  thread.join();
}

TEST(ObjectWaitOneTest, TransientSignalsNotReturned) {
  auto ev = MakeEvent();
  auto main_thread = zx::thread::self();
  std::thread thread([&] {
    ASSERT_OK(WaitForState(main_thread, ZX_THREAD_STATE_BLOCKED_WAIT_ONE));
    ev.signal(/*clear_mask=*/0u, /*set_mask=*/ZX_USER_SIGNAL_0);
    ev.signal(/*clear_mask=*/ZX_USER_SIGNAL_0, /*set_mask=*/0);
    ev.signal(/*clear_mask=*/0u, /*set_mask=*/ZX_USER_SIGNAL_1);
  });
  zx_signals_t observed;
  ASSERT_OK(ev.wait_one(ZX_USER_SIGNAL_1, zx::time::infinite(), &observed));
  ASSERT_EQ(observed, ZX_USER_SIGNAL_1);
  thread.join();
}

TEST(ObjectWaitManyTest, TooManyObjects) {
  zx_wait_item_t items[ZX_WAIT_MANY_MAX_ITEMS + 1];

  for (auto& item : items) {
    item = zx_wait_item_t{
        .handle = MakeEvent().release(), .waitfor = ZX_EVENT_SIGNALED, .pending = 0u};
  }

  ASSERT_EQ(zx::thread::wait_many(items, std::size(items), zx::time::infinite()),
            ZX_ERR_OUT_OF_RANGE);

  for (auto& item : items) {
    ASSERT_OK(zx_handle_close(item.handle));
  }
}

TEST(ObjectWaitManyTest, InvalidHandle) {
  zx_wait_item_t items[3];

  for (auto& item : items) {
    item = zx_wait_item_t{
        .handle = MakeEvent().release(), .waitfor = ZX_EVENT_SIGNALED, .pending = 0u};
  }

  ASSERT_OK(zx_handle_close(items[1].handle));

  ASSERT_EQ(zx::thread::wait_many(items, std::size(items), zx::time::infinite()),
            ZX_ERR_BAD_HANDLE);

  items[1].handle = MakeEvent().release();

  for (auto& item : items) {
    ASSERT_OK(zx_handle_close(item.handle));
  }
}

TEST(ObjectWaitManyTest, WaitForEventsSignaled) {
  zx_wait_item_t items[8];
  for (auto& item : items) {
    item = zx_wait_item_t{
        .handle = MakeEvent().release(), .waitfor = ZX_EVENT_SIGNALED, .pending = 0u};
  }
  // Signal some.
  zx_signals_t to_signal[8] = {0u, 0u, ZX_EVENT_SIGNALED, 0u, 0u, ZX_EVENT_SIGNALED, 0u, 0u};
  for (size_t ix = 0; ix != std::size(to_signal); ++ix) {
    if (to_signal[ix]) {
      ASSERT_OK(zx_object_signal(items[ix].handle, 0u, to_signal[ix]));
    }
  }
  ASSERT_OK(zx::thread::wait_many(items, std::size(items), zx::time::infinite()));
  for (size_t ix = 0; ix != std::size(to_signal); ++ix) {
    ASSERT_EQ(items[ix].pending, to_signal[ix]);
    ASSERT_OK(zx_handle_close(items[ix].handle));
  }
}

TEST(ObjectWaitManyTest, WaitForEventsThenSignal) {
  zx_wait_item_t items[8];
  for (auto& item : items) {
    item = zx_wait_item_t{
        .handle = MakeEvent().release(), .waitfor = ZX_EVENT_SIGNALED, .pending = 0u};
  }
  auto main_thread = zx::thread::self();
  zx_signals_t to_signal[8] = {0u, ZX_EVENT_SIGNALED, 0u, 0u, 0u, 0u, ZX_EVENT_SIGNALED, 0u};
  std::thread thread([&] {
    ASSERT_OK(WaitForState(main_thread, ZX_THREAD_STATE_BLOCKED_WAIT_MANY));
    for (size_t ix = 0; ix != std::size(to_signal); ++ix) {
      if (to_signal[ix]) {
        ASSERT_OK(zx_object_signal(items[ix].handle, 0u, to_signal[ix]));
      }
    }
  });

  ASSERT_OK(zx::thread::wait_many(items, std::size(items), zx::time::infinite()));
  thread.join();

  int signal_count = 0;
  for (size_t ix = 0; ix != std::size(to_signal); ++ix) {
    signal_count += (items[ix].pending == ZX_EVENT_SIGNALED) ? 1 : 0;
    ASSERT_OK(zx_handle_close(items[ix].handle));
  }
  // depending on timing, the client might not see all the signaled events, but since
  // the wait completed, at least one of them is signaled.
  EXPECT_GT(signal_count, 0);
}

TEST(ObjectWaitManyTest, TransientSignalsNotReturned) {
  // Wait on a USER_SIGNAL_0 on three objects.
  zx_wait_item_t items[3];
  for (auto& item : items) {
    item = zx_wait_item_t{
        .handle = MakeEvent().release(),
        .waitfor = ZX_USER_SIGNAL_0,
        .pending = 0u,
    };
  }

  auto main_thread = zx::thread::self();
  std::thread thread([&] {
    // Wait for the main thread to get ready.
    ASSERT_OK(WaitForState(main_thread, ZX_THREAD_STATE_BLOCKED_WAIT_MANY));

    // Assert and clear USER_SIGNAL_1 on handle 0 and 2.
    ASSERT_OK(zx_object_signal(items[0].handle, /*clear_mask=*/0u, /*set_mask=*/ZX_USER_SIGNAL_1));
    ASSERT_OK(zx_object_signal(items[0].handle, /*clear_mask=*/ZX_USER_SIGNAL_1, /*set_mask=*/0u));
    ASSERT_OK(zx_object_signal(items[2].handle, /*clear_mask=*/0u, /*set_mask=*/ZX_USER_SIGNAL_1));
    ASSERT_OK(zx_object_signal(items[2].handle, /*clear_mask=*/ZX_USER_SIGNAL_1, /*set_mask=*/0u));

    // Assert USER_SIGNAL_1 on handle 1.
    ASSERT_OK(zx_object_signal(items[1].handle, /*clear_mask=*/0u, /*set_mask=*/ZX_USER_SIGNAL_1));

    // Assert USER_SIGNAL_0 on handle 0.
    ASSERT_OK(zx_object_signal(items[0].handle, /*clear_mask=*/0u, /*set_mask=*/ZX_USER_SIGNAL_0));
  });

  // Wait for the signals.
  ASSERT_OK(zx::thread::wait_many(items, std::size(items), zx::time::infinite()));
  thread.join();

  // The transient USER_SIGNAL_1 signal on objects 0 and 2 should not be set.
  // However, the asserted (but non-watched) USER_SIGNAL_1 on object 1 should be set,
  // and the asserted (and watched) USER_SIGNAL_0 should be set on object 0.
  EXPECT_EQ(items[0].pending, ZX_USER_SIGNAL_0);
  EXPECT_EQ(items[1].pending, ZX_USER_SIGNAL_1);
  EXPECT_EQ(items[2].pending, 0);
}

TEST(ObjectWaitManyTest, WaitOnZeroThings) {
  zx::time before = zx::clock::get_monotonic();

  // Time out 100 milliseconds from now.
  zx::time deadline = before + zx::msec(100);

  zx_status_t status = zx::handle::wait_many(nullptr, 0, deadline);

  zx::time after = zx::clock::get_monotonic();

  // The wait_many call should have "successfully" timed out, rather
  // than reporting invalid args or some other error.
  EXPECT_EQ(status, ZX_ERR_TIMED_OUT);

  // Time should have advanced, at least 10 milliseconds.
  EXPECT_TRUE(after > before);
  zx::duration delta = after - before;
  EXPECT_TRUE(delta > zx::msec(10));
}

}  // namespace
