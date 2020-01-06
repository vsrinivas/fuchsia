// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/testing/cpp/fidl.h>

#include <thread>

#include <gtest/gtest.h>
#include <sdk/lib/sys/cpp/component_context.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

#define ASSERT_OK(a) ASSERT_EQ(a, ZX_OK)

namespace mock_clock = fuchsia::testing;

class FakeClockTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    auto ctx = sys::ComponentContext::Create();
    ctx->svc()->Connect(mock_clock.NewRequest());
    // always pause mock clock before test starts
    ASSERT_OK(mock_clock->Pause());
  }

  static mock_clock::Increment MakeIncrement(zx::duration dur) {
    mock_clock::Increment ret;
    ret.set_determined(dur.to_nsecs());
    return ret;
  }

  void Advance(zx::duration dur) {
    mock_clock::FakeClockControl_Advance_Result result;
    ASSERT_OK(mock_clock->Advance(MakeIncrement(dur), &result));
    ASSERT_TRUE(result.is_response());
  }

  static zx::time GetTime() { return zx::time(zx_clock_get_monotonic()); }

  mock_clock::FakeClockControlSyncPtr mock_clock;
};

TEST_F(FakeClockTest, get_monotonic) {
  auto t1 = GetTime();
  auto adv = zx::msec(500);
  Advance(adv);
  auto t2 = GetTime();
  ASSERT_EQ(t1 + adv, t2);
}

TEST_F(FakeClockTest, deadline_after) {
  auto t1 = GetTime();
  auto t2 = zx::deadline_after(zx::msec(500));
  ASSERT_EQ(t1 + zx::msec(500), t2);
}

TEST_F(FakeClockTest, nanosleep) {
  bool done = false;
  auto deadline = zx::deadline_after(zx::msec(500));
  std::thread thread([&done, deadline]() {
    zx_nanosleep(deadline.get());
    done = true;
  });
  Advance(zx::msec(250));
  ASSERT_FALSE(done);
  Advance(zx::msec(250));
  thread.join();
  ASSERT_TRUE(done);
}

TEST_F(FakeClockTest, clock_get) {
  Advance(zx::sec(90));
  auto t1 = GetTime();
  zx_time_t t2;
  ASSERT_OK(zx_clock_get(ZX_CLOCK_MONOTONIC, &t2));
  ASSERT_EQ(t2, t1.get());
}

TEST_F(FakeClockTest, object_wait_one_timeout) {
  zx_status_t status;
  zx_signals_t signals;
  auto deadline = zx::deadline_after(zx::msec(500));
  zx::event te;
  ASSERT_OK(zx::event::create(0, &te));
  std::thread thread([&status, deadline, &te, &signals]() {
    status = te.wait_one(ZX_EVENT_SIGNALED, deadline, &signals);
  });
  Advance(zx::msec(500));
  thread.join();
  ASSERT_EQ(status, ZX_ERR_TIMED_OUT);
  ASSERT_EQ(signals, ZX_SIGNAL_NONE);
}

TEST_F(FakeClockTest, object_wait_one_signal) {
  zx_status_t status;
  zx_signals_t signals;
  auto deadline = zx::deadline_after(zx::msec(500));
  zx::event te;
  ASSERT_OK(zx::event::create(0, &te));
  std::thread thread([&status, deadline, &te, &signals]() {
    status = te.wait_one(ZX_EVENT_SIGNALED, deadline, &signals);
  });
  te.signal(0, ZX_EVENT_SIGNALED);
  thread.join();
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(signals, ZX_EVENT_SIGNALED);
}

TEST_F(FakeClockTest, object_wait_many_timeout_small) {
  zx_status_t status;

  auto deadline = zx::deadline_after(zx::msec(500));
  zx::event e1, e2;
  ASSERT_OK(zx::event::create(0, &e1));
  ASSERT_OK(zx::event::create(0, &e2));
  zx_wait_item_t wait[] = {
      {.handle = e1.get(), .waitfor = ZX_EVENT_SIGNALED, .pending = 0},
      {.handle = e2.get(), .waitfor = ZX_EVENT_SIGNALED, .pending = 0},
  };

  std::thread thread(
      [&status, deadline, &wait]() { status = zx_object_wait_many(wait, 2, deadline.get()); });

  Advance(zx::msec(500));
  thread.join();
  ASSERT_EQ(status, ZX_ERR_TIMED_OUT);
  ASSERT_EQ(wait[0].pending, ZX_SIGNAL_NONE);
  ASSERT_EQ(wait[1].pending, ZX_SIGNAL_NONE);
};

TEST_F(FakeClockTest, object_wait_many_signal_small) {
  zx_status_t status;

  auto deadline = zx::deadline_after(zx::msec(500));
  zx::event e1, e2;
  ASSERT_OK(zx::event::create(0, &e1));
  ASSERT_OK(zx::event::create(0, &e2));
  ASSERT_OK(e1.signal(0, ZX_EVENT_SIGNALED));
  zx_wait_item_t wait[] = {
      {.handle = e1.get(), .waitfor = ZX_EVENT_SIGNALED, .pending = 0},
      {.handle = e2.get(), .waitfor = ZX_EVENT_SIGNALED, .pending = 0},
  };

  std::thread thread(
      [&status, deadline, &wait]() { status = zx_object_wait_many(wait, 2, deadline.get()); });
  thread.join();
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(wait[0].pending, ZX_EVENT_SIGNALED);
  ASSERT_EQ(wait[1].pending, ZX_SIGNAL_NONE);
};

TEST_F(FakeClockTest, object_wait_many_timeout_large) {
  zx_status_t status;

  auto deadline = zx::deadline_after(zx::msec(500));
  constexpr size_t kEventCount = ZX_WAIT_MANY_MAX_ITEMS;
  std::vector<zx::event> events;
  zx_wait_item_t wait[kEventCount];
  for (auto& i : wait) {
    zx::event e;
    ASSERT_OK(zx::event::create(0, &e));
    i = {.handle = e.get(), .waitfor = ZX_EVENT_SIGNALED, .pending = 0};
    events.push_back(std::move(e));
  };

  std::thread thread([&status, deadline, &wait]() {
    status = zx_object_wait_many(wait, kEventCount, deadline.get());
  });

  Advance(zx::msec(500));
  thread.join();
  ASSERT_EQ(status, ZX_ERR_TIMED_OUT);
  for (auto& i : wait) {
    ASSERT_EQ(i.pending, ZX_SIGNAL_NONE);
  }
};

TEST_F(FakeClockTest, object_wait_many_signal_large) {
  zx_status_t status;

  auto deadline = zx::deadline_after(zx::msec(500));
  constexpr size_t kEventCount = ZX_WAIT_MANY_MAX_ITEMS;
  std::vector<zx::event> events;
  zx_wait_item_t wait[kEventCount];
  for (auto& i : wait) {
    zx::event e;
    ASSERT_OK(zx::event::create(0, &e));
    i = {.handle = e.get(), .waitfor = ZX_EVENT_SIGNALED, .pending = 0};
    events.push_back(std::move(e));
  };
  // signal the first and last events
  ASSERT_OK(events[0].signal(0, ZX_EVENT_SIGNALED));
  ASSERT_OK(events[ZX_WAIT_MANY_MAX_ITEMS - 1].signal(0, ZX_EVENT_SIGNALED));

  std::thread thread([&status, deadline, &wait]() {
    status = zx_object_wait_many(wait, kEventCount, deadline.get());
  });

  thread.join();
  ASSERT_EQ(status, ZX_OK);
  // check return on first and last events
  ASSERT_EQ(wait[0].pending, ZX_EVENT_SIGNALED);
  ASSERT_EQ(wait[kEventCount - 1].pending, ZX_EVENT_SIGNALED);
  for (size_t i = 1; i < kEventCount - 1; i++) {
    ASSERT_EQ(wait[i].pending, ZX_SIGNAL_NONE);
  }
};

TEST_F(FakeClockTest, port_wait_timeout) {
  zx_status_t status;
  auto deadline = zx::deadline_after(zx::msec(500));
  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));
  zx_port_packet_t packet;
  std::thread thread(
      [&status, deadline, &port, &packet]() { status = port.wait(deadline, &packet); });
  Advance(zx::msec(500));
  thread.join();
  ASSERT_EQ(status, ZX_ERR_TIMED_OUT);
}

TEST_F(FakeClockTest, port_wait_packet) {
  zx_status_t status;
  auto deadline = zx::deadline_after(zx::msec(500));
  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));
  zx_port_packet_t packet;
  std::thread thread(
      [&status, deadline, &port, &packet]() { status = port.wait(deadline, &packet); });
  zx_port_packet_t snd;
  snd.type = ZX_PKT_TYPE_USER;
  snd.key = 0xAABB;
  snd.user.u64[0] = 0x2020;
  ASSERT_OK(port.queue(&snd));
  thread.join();
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(packet.type, ZX_PKT_TYPE_USER);
  ASSERT_EQ(packet.key, snd.key);
  ASSERT_EQ(packet.user.u64[0], snd.user.u64[0]);
  Advance(zx::msec(500));
  // ensure that we won't see the old injected port event.
  ASSERT_EQ(port.wait(deadline, &packet), ZX_ERR_TIMED_OUT);
}

TEST_F(FakeClockTest, timer_fire) {
  auto deadline = zx::deadline_after(zx::msec(500));
  zx::timer timer;
  ASSERT_OK(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer));
  ASSERT_OK(timer.set(deadline, zx::msec(10)));
  Advance(zx::msec(500));
  zx_signals_t signals;
  ASSERT_OK(timer.wait_one(ZX_TIMER_SIGNALED, zx::time::infinite(), &signals));
  ASSERT_EQ(signals, ZX_TIMER_SIGNALED);
}

TEST_F(FakeClockTest, timer_cancel) {
  auto deadline = zx::deadline_after(zx::msec(500));
  zx::timer timer;
  ASSERT_OK(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer));
  // set timer to some deadline
  ASSERT_OK(timer.set(deadline, zx::msec(10)));
  zx_signals_t signals;
  // cancel and then advance the timer, check that it wasn't signaled
  ASSERT_OK(timer.cancel());
  Advance(zx::msec(500));
  ASSERT_EQ(timer.wait_one(ZX_TIMER_SIGNALED, zx::time(0), &signals), ZX_ERR_TIMED_OUT);

  deadline = zx::deadline_after(zx::msec(500));
  ASSERT_OK(timer.set(deadline, zx::msec(10)));
  // trigger and then cancel the timer, cancelling the timer MUST clear the signaled bit
  Advance(zx::msec(500));
  ASSERT_EQ(timer.wait_one(ZX_TIMER_SIGNALED, zx::time(0), &signals), ZX_OK);
  ASSERT_OK(timer.cancel());
  ASSERT_EQ(timer.wait_one(ZX_TIMER_SIGNALED, zx::time(0), &signals), ZX_ERR_TIMED_OUT);
}
