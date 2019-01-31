// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <cobalt-client/cpp/timer.h>
#include <lib/fzl/time.h>
#include <lib/zx/time.h>
#include <unittest/unittest.h>

namespace cobalt_client {
namespace internal {
namespace {

class FakeClock {
public:
    static FakeClock& GetInstance() {
        static FakeClock clock;
        return clock;
    }

    static zx::ticks now() { return GetInstance().current(); }

    zx::ticks current() const { return current_; }

    void set_current(uint32_t current) { current_ = zx::ticks(current); }

private:
    zx::ticks current_ = zx::ticks(0);
};

bool TestCollecting() {
    BEGIN_TEST;
    FakeClock::GetInstance().set_current(1);
    TimerBase<FakeClock> timer(true);
    FakeClock::GetInstance().set_current(4);
    ASSERT_EQ(timer.End().to_nsecs(), fzl::TicksToNs(zx::ticks(3)).to_nsecs());
    END_TEST;
}

bool TestNotCollecting() {
    BEGIN_TEST;
    FakeClock::GetInstance().set_current(1);
    TimerBase<FakeClock> timer(false);
    FakeClock::GetInstance().set_current(4);
    ASSERT_EQ(timer.End().to_nsecs(), fzl::TicksToNs(zx::ticks(0)).to_nsecs());
    END_TEST;
}

bool TestReset() {
    BEGIN_TEST;
    FakeClock::GetInstance().set_current(1);
    TimerBase<FakeClock> timer(true);
    FakeClock::GetInstance().set_current(4);
    timer.Reset();
    FakeClock::GetInstance().set_current(8);
    ASSERT_EQ(timer.End().to_nsecs(), fzl::TicksToNs(zx::ticks(4)).to_nsecs());
    END_TEST;
}

bool TestResetNotCollecting() {
    BEGIN_TEST;
    FakeClock::GetInstance().set_current(1);
    TimerBase<FakeClock> timer(false);
    FakeClock::GetInstance().set_current(4);
    timer.Reset();
    FakeClock::GetInstance().set_current(8);
    ASSERT_EQ(timer.End().to_nsecs(), fzl::TicksToNs(zx::ticks(0)).to_nsecs());
    END_TEST;
}

BEGIN_TEST_CASE(TimerTest)
RUN_TEST(TestCollecting)
RUN_TEST(TestNotCollecting)
RUN_TEST(TestReset)
RUN_TEST(TestResetNotCollecting)
END_TEST_CASE(TimerTest)

} // namespace
} // namespace internal
} // namespace cobalt_client
