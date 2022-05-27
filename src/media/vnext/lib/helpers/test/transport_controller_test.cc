// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/helpers/transport_controller.h"

#include <lib/gtest/real_loop_fixture.h>
#include <lib/zx/clock.h>

#include "src/media/vnext/lib/threads/thread.h"

namespace fmlib {
namespace {

class TransportControllerTest : public gtest::RealLoopFixture {
 public:
  TransportControllerTest() : thread_(fmlib::Thread::CreateForLoop(loop())) {}

  Thread& thread() { return thread_; }

 private:
  Thread thread_;
};

// Tests the static |MakePromiseForTime| method.
TEST_F(TransportControllerTest, MakePromiseForTime) {
  TransportController::Canceler canceler;
  bool promise_completed = false;
  thread().schedule_task(
      TransportController::MakePromiseForTime(thread(), zx::clock::get_monotonic(), &canceler)
          .and_then([&promise_completed]() { promise_completed = true; }));
  EXPECT_FALSE(promise_completed);
  EXPECT_TRUE(canceler.is_valid());

  RunLoopUntil([&promise_completed]() { return promise_completed; });
  EXPECT_FALSE(canceler.is_valid());

  promise_completed = false;
  thread().schedule_task(TransportController::MakePromiseForTime(
                             thread(), zx::clock::get_monotonic() + zx::sec(1), &canceler)
                             .and_then([&promise_completed]() { promise_completed = true; }));
  EXPECT_FALSE(promise_completed);
  EXPECT_TRUE(canceler.is_valid());

  canceler.Cancel();
  EXPECT_FALSE(promise_completed);
  EXPECT_FALSE(canceler.is_valid());

  EXPECT_FALSE(
      RunLoopWithTimeoutOrUntil([&promise_completed]() { return promise_completed; }, zx::sec(2)));
}

// Tests the |MakePromiseFor| method passing a null |when| value.
TEST_F(TransportControllerTest, MakePromiseFor_Now) {
  TransportController under_test;

  TransportController::Canceler canceler;
  bool promise_completed = false;
  thread().schedule_task(
      under_test.MakePromiseFor(thread(), nullptr, &canceler).and_then([&promise_completed]() {
        promise_completed = true;
      }));
  EXPECT_FALSE(promise_completed);
  EXPECT_FALSE(canceler.is_valid());

  RunLoopUntilIdle();
  EXPECT_TRUE(promise_completed);
  EXPECT_FALSE(canceler.is_valid());
}

// Tests the |MakePromiseFor| method passing a system time |when| value.
TEST_F(TransportControllerTest, MakePromiseFor_SystemTime) {
  TransportController under_test;

  TransportController::Canceler canceler;
  bool promise_completed = false;
  auto when = fuchsia::media2::RealOrPresentationTime::New();
  *when = fuchsia::media2::RealOrPresentationTime::WithSystemTime(zx::clock::get_monotonic().get());
  thread().schedule_task(under_test.MakePromiseFor(thread(), std::move(when), &canceler)
                             .and_then([&promise_completed]() { promise_completed = true; }));
  EXPECT_FALSE(promise_completed);
  EXPECT_TRUE(canceler.is_valid());

  RunLoopUntilIdle();
  EXPECT_TRUE(promise_completed);
  EXPECT_FALSE(canceler.is_valid());

  when = fuchsia::media2::RealOrPresentationTime::New();
  promise_completed = false;
  *when = fuchsia::media2::RealOrPresentationTime::WithSystemTime(
      (zx::clock::get_monotonic() + zx::sec(1)).get());
  thread().schedule_task(under_test.MakePromiseFor(thread(), std::move(when), &canceler)
                             .and_then([&promise_completed]() { promise_completed = true; }));
  EXPECT_FALSE(promise_completed);
  EXPECT_TRUE(canceler.is_valid());

  canceler.Cancel();
  EXPECT_FALSE(promise_completed);
  EXPECT_FALSE(canceler.is_valid());

  EXPECT_FALSE(
      RunLoopWithTimeoutOrUntil([&promise_completed]() { return promise_completed; }, zx::sec(2)));
}

// Tests the |MakePromiseFor| method passing a reference time |when| value.
// TODO(dalesat): Test for reference->system conversion when that is implemented.
TEST_F(TransportControllerTest, MakePromiseFor_ReferenceTime) {
  TransportController under_test;

  TransportController::Canceler canceler;
  bool promise_completed = false;
  auto when = fuchsia::media2::RealOrPresentationTime::New();
  *when =
      fuchsia::media2::RealOrPresentationTime::WithReferenceTime(zx::clock::get_monotonic().get());
  thread().schedule_task(under_test.MakePromiseFor(thread(), std::move(when), &canceler)
                             .and_then([&promise_completed]() { promise_completed = true; }));
  EXPECT_FALSE(promise_completed);
  EXPECT_TRUE(canceler.is_valid());

  RunLoopUntilIdle();
  EXPECT_TRUE(promise_completed);
  EXPECT_FALSE(canceler.is_valid());

  when = fuchsia::media2::RealOrPresentationTime::New();
  promise_completed = false;
  *when = fuchsia::media2::RealOrPresentationTime::WithReferenceTime(
      (zx::clock::get_monotonic() + zx::sec(1)).get());
  thread().schedule_task(under_test.MakePromiseFor(thread(), std::move(when), &canceler)
                             .and_then([&promise_completed]() { promise_completed = true; }));
  EXPECT_FALSE(promise_completed);
  EXPECT_TRUE(canceler.is_valid());

  canceler.Cancel();
  EXPECT_FALSE(promise_completed);
  EXPECT_FALSE(canceler.is_valid());

  EXPECT_FALSE(
      RunLoopWithTimeoutOrUntil([&promise_completed]() { return promise_completed; }, zx::sec(2)));
}

// Tests the |MakePromiseFor| method passing a presentation time |when| value.
TEST_F(TransportControllerTest, MakePromiseFor_PresentationTime) {
  constexpr zx::duration kDueTime = zx::nsec(1234);

  TransportController under_test;
  under_test.SetCurrentPresentationTime(kDueTime - zx::nsec(2));

  TransportController::Canceler canceler;
  bool promise_completed = false;
  auto when = fuchsia::media2::RealOrPresentationTime::New();
  *when = fuchsia::media2::RealOrPresentationTime::WithPresentationTime(kDueTime.get());
  thread().schedule_task(under_test.MakePromiseFor(thread(), std::move(when), &canceler)
                             .and_then([&promise_completed]() { promise_completed = true; }));
  EXPECT_FALSE(promise_completed);
  EXPECT_TRUE(canceler.is_valid());

  RunLoopUntilIdle();
  EXPECT_FALSE(promise_completed);
  EXPECT_TRUE(canceler.is_valid());

  under_test.SetCurrentPresentationTime(kDueTime - zx::nsec(1));

  RunLoopUntilIdle();
  EXPECT_FALSE(promise_completed);
  EXPECT_TRUE(canceler.is_valid());

  under_test.SetCurrentPresentationTime(kDueTime);

  RunLoopUntilIdle();
  EXPECT_TRUE(promise_completed);
  EXPECT_FALSE(canceler.is_valid());

  under_test.SetCurrentPresentationTime(kDueTime - zx::nsec(2));

  when = fuchsia::media2::RealOrPresentationTime::New();
  promise_completed = false;
  *when = fuchsia::media2::RealOrPresentationTime::WithPresentationTime(kDueTime.get());
  thread().schedule_task(under_test.MakePromiseFor(thread(), std::move(when), &canceler)
                             .and_then([&promise_completed]() { promise_completed = true; }));
  EXPECT_FALSE(promise_completed);
  EXPECT_TRUE(canceler.is_valid());

  RunLoopUntilIdle();
  EXPECT_FALSE(promise_completed);
  EXPECT_TRUE(canceler.is_valid());

  canceler.Cancel();
  EXPECT_FALSE(promise_completed);
  EXPECT_FALSE(canceler.is_valid());

  EXPECT_FALSE(
      RunLoopWithTimeoutOrUntil([&promise_completed]() { return promise_completed; }, zx::sec(2)));
}

// Tests the |MakePromiseForPresentationTime| method.
TEST_F(TransportControllerTest, MakePromiseForPresentationTime) {
  constexpr zx::duration kDueTime = zx::nsec(1234);

  TransportController under_test;
  under_test.SetCurrentPresentationTime(kDueTime - zx::nsec(2));

  TransportController::Canceler canceler;
  bool promise_completed = false;
  thread().schedule_task(under_test.MakePromiseForPresentationTime(kDueTime, &canceler)
                             .and_then([&promise_completed]() { promise_completed = true; }));
  EXPECT_FALSE(promise_completed);
  EXPECT_TRUE(canceler.is_valid());

  RunLoopUntilIdle();
  EXPECT_FALSE(promise_completed);
  EXPECT_TRUE(canceler.is_valid());

  under_test.SetCurrentPresentationTime(kDueTime - zx::nsec(1));

  RunLoopUntilIdle();
  EXPECT_FALSE(promise_completed);
  EXPECT_TRUE(canceler.is_valid());

  under_test.SetCurrentPresentationTime(kDueTime);

  RunLoopUntilIdle();
  EXPECT_TRUE(promise_completed);
  EXPECT_FALSE(canceler.is_valid());

  under_test.SetCurrentPresentationTime(kDueTime - zx::nsec(2));

  promise_completed = false;
  thread().schedule_task(under_test.MakePromiseForPresentationTime(kDueTime, &canceler)
                             .and_then([&promise_completed]() { promise_completed = true; }));
  EXPECT_FALSE(promise_completed);
  EXPECT_TRUE(canceler.is_valid());

  RunLoopUntilIdle();
  EXPECT_FALSE(promise_completed);
  EXPECT_TRUE(canceler.is_valid());

  canceler.Cancel();
  EXPECT_FALSE(promise_completed);
  EXPECT_FALSE(canceler.is_valid());

  EXPECT_FALSE(
      RunLoopWithTimeoutOrUntil([&promise_completed]() { return promise_completed; }, zx::sec(2)));
}

// Tests the |CancelAllPresentationTimePromises| method.
TEST_F(TransportControllerTest, CancelAllPresentationTimePromises) {
  constexpr zx::duration kDueTime = zx::nsec(1234);

  TransportController under_test;
  under_test.SetCurrentPresentationTime(kDueTime - zx::nsec(2));

  TransportController::Canceler canceler;
  bool promise_failed = false;
  thread().schedule_task(
      under_test.MakePromiseForPresentationTime(kDueTime, &canceler).or_else([&promise_failed]() {
        promise_failed = true;
      }));
  EXPECT_FALSE(promise_failed);
  EXPECT_TRUE(canceler.is_valid());

  RunLoopUntilIdle();
  EXPECT_FALSE(promise_failed);
  EXPECT_TRUE(canceler.is_valid());

  under_test.CancelAllPresentationTimePromises();
  RunLoopUntilIdle();
  EXPECT_TRUE(promise_failed);
  EXPECT_FALSE(canceler.is_valid());
}

}  // namespace
}  // namespace fmlib
