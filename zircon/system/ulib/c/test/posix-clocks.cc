// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <lib/zx/clock.h>
#include <sys/time.h>
#include <time.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <zircon/utc.h>

#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

namespace {

enum class FixtureType {
  kNoClock,
  kReadOnlyClock,
  kReadWriteClock,
};

template <FixtureType _Type>
class UtcFixture : public zxtest::Test {
 public:
  static constexpr FixtureType Type = _Type;
  static constexpr int64_t kBackstopTime = 123456789;

  void SetUp() override {
    ASSERT_FALSE(clock_installed_);
    zx::clock clock_to_install;

    auto cleanup = fbl::MakeAutoCall([this]() {
      ZX_ASSERT(!clock_installed_);
      test_clock_.reset();
      runtime_clock_.reset();
    });

    // If we are using a clock in this test case, go ahead and make it now.
    if constexpr (Type != FixtureType::kNoClock) {
      zx_clock_create_args_v1_t create_args{.backstop_time = kBackstopTime};
      ASSERT_OK(zx::clock::create(0, &create_args, &test_clock_));

      // Fetch its rights, and make a duplicate handle to provide to the
      // runtime, reducing the rights of the clock if needed.
      zx_info_handle_basic_t info;
      ASSERT_OK(test_clock_.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
      if constexpr (Type == FixtureType::kReadOnlyClock) {
        info.rights &= ~ZX_RIGHT_WRITE;
      }

      ASSERT_OK(test_clock_.duplicate(info.rights, &clock_to_install));
    }

    ASSERT_OK(
        zx_utc_reference_swap(clock_to_install.release(), runtime_clock_.reset_and_get_address()));
    clock_installed_ = true;
    cleanup.cancel();
  }

  void TearDown() override {
    // If we had replaced the UTC reference, restore it back to what it had been.
    if (clock_installed_) {
      zx::clock release_me;
      zx_utc_reference_swap(runtime_clock_.release(), release_me.reset_and_get_address());
      clock_installed_ = false;
    } else {
      runtime_clock_.reset();
    }

    test_clock_.reset();
  }

  zx_status_t test_clock_set_value(zx::time val) const {
    // NoClock tests cannot set the clock and should never even try
    static_assert(Type != FixtureType::kNoClock);
    return test_clock_.update(zx::clock::update_args().set_value(val));
  }

  zx_time_t test_clock_get_now() const {
    if constexpr (Type == FixtureType::kNoClock) {
      // TODO(johngro): Clean this up when UTC in the kernel goes away.  For
      // now, if there is no handle based clock available to the runtime, it
      // will fall back on kernel UTC.  Once we switch away from that, these
      // tests will need to be updated to expect the behavior we choose to
      // implement in the case where a runtime is not provided a UTC reference
      // at startup.
      zx::time_utc ret;
      zx::clock::get(&ret);
      return ret.get();
    } else {
      // This should never fail.  If it does, it is an indication of
      // panic-worthy corruption in our test environment.
      zx_time_t ret;
      zx_status_t res = test_clock_.read(&ret);
      ZX_ASSERT(res == ZX_OK);
      return ret;
    }
  }

  void test_clock_get_details(zx_clock_details_v1* details_out) {
    ASSERT_NOT_NULL(details_out);
    ASSERT_OK(test_clock_.get_details(details_out));
  }

 protected:
  zx::clock test_clock_;
  zx::clock runtime_clock_;
  bool clock_installed_ = false;
};

// Aliases for the various fixture types.  The test framework macros do not like
// to see <> in their parameters.
using NoUtcClockTestCase = UtcFixture<FixtureType::kNoClock>;
using ReadOnlyUtcClockTestCase = UtcFixture<FixtureType::kReadOnlyClock>;
using ReadWriteUtcClockTestCase = UtcFixture<FixtureType::kReadWriteClock>;

zx_time_t unpack_timespec(const struct timespec& ts) {
  return ZX_SEC(ts.tv_sec) + ZX_NSEC(ts.tv_nsec);
}

zx_time_t unpack_timeval(const struct timeval& tv) {
  return ZX_SEC(tv.tv_sec) + ZX_USEC(tv.tv_usec);
}

constexpr inline zx_time_t round_to_usec(zx_time_t val) { return (val / ZX_USEC(1)) * ZX_USEC(1); }

template <typename Fixture>
void TestGetTime(const Fixture& fixture) {
  // When the test starts, we expect the clock to not be running yet, and to
  // report only its backstop time when read, even if we put some reasonably
  // significant delays in the observations.
  zx_time_t before, after;
  zx_time_t unpacked_clock_gettime;
  zx_time_t unpacked_gettimeofday;

  auto observe_clocks = [&]() {
    struct timespec clock_gettime_ts;
    struct timeval gettimeofday_tv;

    before = fixture.test_clock_get_now();
    zx::nanosleep(zx::deadline_after(zx::msec(1)));
    ASSERT_EQ(0, clock_gettime(CLOCK_REALTIME, &clock_gettime_ts));
    zx::nanosleep(zx::deadline_after(zx::msec(1)));
    ASSERT_EQ(0, gettimeofday(&gettimeofday_tv, nullptr));
    zx::nanosleep(zx::deadline_after(zx::msec(1)));
    after = fixture.test_clock_get_now();

    unpacked_clock_gettime = unpack_timespec(clock_gettime_ts);
    unpacked_gettimeofday = unpack_timeval(gettimeofday_tv);
  };

  // Check the ordering of an observation.
  //
  // Both the clock_gettime and the gettimeofday observations should exist
  // between the before/after range, but the gettimeofday observation is limited
  // to uSec resolution which needs to be accounted for.  Also, check to make
  // sure that gettimeofday comes after clock_gettime (again, limited by the
  // resolution of gettimeofday)
  auto check_ordering = [&]() {
    ASSERT_LE(before, unpacked_clock_gettime);
    ASSERT_GE(after, unpacked_clock_gettime);
    ASSERT_LE(round_to_usec(before), unpacked_gettimeofday);
    ASSERT_GE(round_to_usec(after), unpacked_gettimeofday);
    ASSERT_LE(round_to_usec(unpacked_clock_gettime), unpacked_gettimeofday);
  };

  ASSERT_NO_FATAL_FAILURES(observe_clocks());

  if constexpr (Fixture::Type == FixtureType::kNoClock) {
    // The NoClock version of this test cannot rely on any backstop behavior, nor will it ever be
    // able to set the clock.  All we can do is assert an ordering to our observations.  Both the
    // clock_gettime and the gettimeofday observations should exist between the before/after range
    ASSERT_NO_FATAL_FAILURES(check_ordering());
  } else {
    ASSERT_EQ(Fixture::kBackstopTime, before);
    ASSERT_EQ(Fixture::kBackstopTime, unpacked_clock_gettime);
    ASSERT_EQ(round_to_usec(Fixture::kBackstopTime), unpacked_gettimeofday);
    ASSERT_EQ(Fixture::kBackstopTime, after);

    // OK, now start our test clock.  We'll put it at a point which is 2x ahead of
    // our arbitrary backstop.
    zx::time start_time{Fixture::kBackstopTime * 2};
    ASSERT_OK(fixture.test_clock_set_value(start_time));

    // Now observe the clock via clock_gettime and make sure it all makes sense.
    ASSERT_NO_FATAL_FAILURES(observe_clocks());

    // No observations can come before start_time
    ASSERT_LE(start_time.get(), before);
    ASSERT_LE(start_time.get(), unpacked_clock_gettime);
    ASSERT_LE(round_to_usec(start_time.get()), unpacked_gettimeofday);
    ASSERT_LE(start_time.get(), after);

    // Ordering should match the order of query.
    ASSERT_NO_FATAL_FAILURES(check_ordering());

    // Jump the clock ahead by an absurd amount (let's use 7 days) and observe
    // again.  Make sure that clock_gettime is following along with us.  Same
    // checks as before, different start time.
    start_time += zx::sec(86400 * 7);
    ASSERT_OK(fixture.test_clock_set_value(start_time));

    ASSERT_NO_FATAL_FAILURES(observe_clocks());

    ASSERT_LE(start_time.get(), before);
    ASSERT_LE(start_time.get(), unpacked_clock_gettime);
    ASSERT_LE(round_to_usec(start_time.get()), unpacked_gettimeofday);
    ASSERT_LE(start_time.get(), after);
    ASSERT_NO_FATAL_FAILURES(check_ordering());
  }
}

TEST_F(NoUtcClockTestCase, GetTime) {
  // With no clock at all, we currently expect to just get what the kernel UTC
  // reports.  In the future, we expect some form of reasonable failure.
  //
  // :: FLAKE-ALERT ::
  //
  // If something adjusts the kernel wide UTC clock while this test is running,
  // it might cause the test to flake.  This problem will go away once we move
  // to handle based clocks.  All of the other tests in this file inject a test
  // clock into the runtime, which not only gives them control of the clock for
  // testing purposes, but also prevents any chance of flake as the test clock
  // is exclusively controlled by the test environment.
  ASSERT_NO_FAILURES(TestGetTime(*this));
}

TEST_F(ReadOnlyUtcClockTestCase, GetTime) { ASSERT_NO_FAILURES(TestGetTime(*this)); }

TEST_F(ReadWriteUtcClockTestCase, GetTime) { ASSERT_NO_FAILURES(TestGetTime(*this)); }

// Zircon will always report a clock resolution based on the underlying tick
// resolution, since all time-keeping in the kernel is based on the underlying
// resolution of the tick counter.  Currently, while the kernel is aware of the
// underlying resolution of the tick counter as a ratio, we only expose it to
// users as a 64 bit number of "ticks per second".  Because of this, we need to
// deal with the case where the number of ticks per second of the underlying
// tick counter does not evenly divide 1e9.
//
// Right now, we expect the Fuchsia implementation of clock_getres to return the
// value 1e9 / ticks_per_second subjected to C's integer rounding rules (IOW -
// rounded down).  If this assumption changes, this test will fail and
// (hopefully) the individual changing the code will come and read the comment
// and fix the test (or implementation, or both).
//
// On a related note, the tick counter on some systems can count at rates >
// 1GHz.  In particular, the tick counter on x64 systems based on an invariant
// TSC can end up counting at the CPU's top clock rate, which is usually
// significantly higher than 1GHz.  In this case, clock_getres is expect to
// handle this special case by returning the smallest non-zero period which can
// be represented using the timespec structure as defined today.  IOW - we
// expect tick counters which tick at more than 1GHz to report the nSec-per-tick
// to be 1nSec instead of 0.
//
template <typename Fixture>
void TestGetRes(const Fixture& fixture) {
  struct timespec res;

  uint64_t nsec_per_tick = ZX_SEC(1) / zx_ticks_per_second();
  if (nsec_per_tick == 0) {
    nsec_per_tick = 1;
  }

  ASSERT_EQ(0, clock_getres(CLOCK_REALTIME, &res));
  ASSERT_EQ(nsec_per_tick / ZX_SEC(1), res.tv_sec);
  ASSERT_EQ(nsec_per_tick % ZX_SEC(1), res.tv_nsec);
}

TEST_F(NoUtcClockTestCase, GetRes) { ASSERT_NO_FAILURES(TestGetRes(*this)); }

TEST_F(ReadOnlyUtcClockTestCase, GetRes) { ASSERT_NO_FAILURES(TestGetRes(*this)); }

TEST_F(ReadWriteUtcClockTestCase, GetRes) { ASSERT_NO_FAILURES(TestGetRes(*this)); }

// If no clock has been provided to the system, or the clock provided is read
// only, any attempt to set it should fail with a permission error.
template <typename Fixture>
void TestSetUnsettableClock(const Fixture& fixture) {
  struct timespec target;

  // Don't try to set a time before the backstop time.  It does not really
  // matter here since we expect the set operation to fail, but we want to make
  // sure that it fails because we are fundamentally not allowed to set the
  // clock, not because we tried to roll the clock back to before the backstop.
  constexpr zx_time_t kAfterBackstop = Fixture::kBackstopTime * 2;
  target.tv_sec = kAfterBackstop / ZX_SEC(1);
  target.tv_nsec = kAfterBackstop % ZX_SEC(1);
  errno = 0;
  ASSERT_EQ(-1, clock_settime(CLOCK_REALTIME, &target));
  ASSERT_EQ(EPERM, errno);

  // Try again with settimeofday.  We should get the same result.
  struct timeval target_tv;
  target_tv.tv_sec = target.tv_sec;
  target_tv.tv_usec = target.tv_nsec / 1000;
  errno = 0;
  ASSERT_EQ(-1, settimeofday(&target_tv, NULL));
  ASSERT_EQ(EPERM, errno);
}

TEST_F(NoUtcClockTestCase, SetTime) { ASSERT_NO_FAILURES(TestSetUnsettableClock(*this)); }

TEST_F(ReadOnlyUtcClockTestCase, SetTime) { ASSERT_NO_FAILURES(TestSetUnsettableClock(*this)); }

TEST_F(ReadWriteUtcClockTestCase, SetTime) {
  // OK, we are in a test environment where we expect to be able to set our
  // clock.  Let's start with trying to set the clock to a time before the
  // backstop time.  This request should be denied with EINVAL.
  struct timespec ts;
  ts.tv_sec = 0;
  ts.tv_nsec = 0;
  errno = 0;
  ASSERT_EQ(-1, clock_settime(CLOCK_REALTIME, &ts));
  ASSERT_EQ(EINVAL, errno);

  // Same idea, but this time using settimeofday instead.
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  errno = 0;
  ASSERT_EQ(-1, settimeofday(&tv, NULL));
  ASSERT_EQ(EINVAL, errno);

  // Now, set this clock, but this time in a way we expect will succeed.  We can
  // use the get_detail's method of the clock to read the transformation which
  // was actually set.  Note, that there are zillion different valid mono <->
  // synthetic transformations that we _might_ observe in the get_details
  // results, but we are going to (for now) take advantage of how we _know_ the
  // kernel implementation actually sets a clock in order to check that our
  // results were applied properly.  In specific, we know that the time we set
  // (expressed in nanoseconds) is going to the be synthetic offset in the clock
  // after the set operation, both for the mono <-> transformation, as well as
  // the ticks <-> transformation.
  constexpr zx_time_t kAfterBackstop = kBackstopTime * 2;
  ts.tv_sec = kAfterBackstop / ZX_SEC(1);
  ts.tv_nsec = kAfterBackstop % ZX_SEC(1);
  ASSERT_EQ(0, clock_settime(CLOCK_REALTIME, &ts));

  zx_clock_details_v1_t details;
  ASSERT_NO_FATAL_FAILURES(test_clock_get_details(&details));

  zx_time_t expected = unpack_timespec(ts);
  ASSERT_EQ(expected, details.ticks_to_synthetic.synthetic_offset);
  ASSERT_EQ(expected, details.mono_to_synthetic.synthetic_offset);

  // Same trick, but using settimeofday instead.  We should see a synthetic
  // offset which is limited to uSec resolution, and a reference offset which is
  // >= the previous reference offset (since this set operation came after the
  // previous one).
  tv.tv_sec = ts.tv_sec;
  tv.tv_usec = ts.tv_nsec / ZX_USEC(1);
  ASSERT_EQ(0, settimeofday(&tv, NULL));

  zx_clock_details_v1_t details2;
  ASSERT_NO_FATAL_FAILURES(test_clock_get_details(&details2));

  expected = unpack_timeval(tv);
  ASSERT_EQ(expected, details2.ticks_to_synthetic.synthetic_offset);
  ASSERT_EQ(expected, details2.mono_to_synthetic.synthetic_offset);
  ASSERT_LE(details.ticks_to_synthetic.reference_offset,
            details2.ticks_to_synthetic.reference_offset);
  ASSERT_LE(details.mono_to_synthetic.reference_offset,
            details2.mono_to_synthetic.reference_offset);
}

TEST(PosixClockTests, BootTimeIsMonotonicTime) {
  // The test strategy here is limited, as we do not have a straightforward
  // mechanism with which to modify the underlying syscall behavior. We switch
  // back and forward between calling clock_gettime with CLOCK_MONOTONIC and
  // CLOCK_BOOTTIME, and assert their relative monotonicity. This test ensures
  // that these calls succeed, and that time is at least frozen, if not
  // increasing in a monotonic fashion, with repect to both clock ids.

  timespec last{};  // Zero is before the first sample.

  int which = 0;
  for (int i = 0; i < 100; i++) {
    timespec ts;

    switch (which) {
      case 0:
        ASSERT_EQ(0, clock_gettime(CLOCK_MONOTONIC, &ts), "%s", strerror(errno));
        break;
      case 1:
        ASSERT_EQ(0, clock_gettime(CLOCK_BOOTTIME, &ts), "%s", strerror(errno));
        break;
      case 2:
        ASSERT_EQ(0, clock_gettime(CLOCK_MONOTONIC_RAW, &ts), "%s", strerror(errno));
        break;
    }

    if (ts.tv_sec == last.tv_sec) {
      EXPECT_GE(ts.tv_nsec, last.tv_nsec, "clock_gettime(CLOCK_{MONOTONIC,BOOTTIME})");
    } else {
      EXPECT_GE(ts.tv_sec, last.tv_sec, "clock_gettime(CLOCK_{MONOTONIC,BOOTTIME})");
    }

    if (++which % 3 == 0) {
      which = 0;
    }

    last = ts;
  }
}

}  // namespace
