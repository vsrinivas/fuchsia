// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/utils/clock.h"

#include <fuchsia/time/cpp/fidl.h>
#include <fuchsia/time/cpp/fidl_test_base.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

#include <future>
#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/gtest/test_loop_fixture.h"

namespace cobalt {

using namespace testing;

class FakeUtcImpl : public fuchsia::time::testing::Utc_TestBase {
 public:
  FakeUtcImpl() = default;

  void NotImplemented_(const std::string& name) final {
    ASSERT_TRUE(false) << name << " is not implemented";
  }

  void WatchState(WatchStateCallback callback) override { callback(std::move(*MockWatchState())); }

  MOCK_METHOD0(MockWatchState, fuchsia::time::UtcState*());
};

class FuchsiaSystemClockTest : public ::gtest::TestLoopFixture {
 public:
  ~FuchsiaSystemClockTest() override = default;

 protected:
  void SetUp() override {
    auto service_provider = context_provider_.service_directory_provider();
    service_provider->AddService<fuchsia::time::Utc>(utc_bindings_.GetHandler(&utc_impl_));
    clock_ = std::make_unique<FuchsiaSystemClock>(service_provider->service_directory());
  }

  void TearDown() override {
    clock_.reset();
    utc_bindings_.CloseAll();
  }

  std::unique_ptr<FuchsiaSystemClock> clock_;
  FakeUtcImpl utc_impl_{};

 private:
  fidl::BindingSet<fuchsia::time::Utc> utc_bindings_;
  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(FuchsiaSystemClockTest, AwaitUnverifiedSourceInitiallyAccurate) {
  fuchsia::time::UtcState utc_state;
  utc_state.set_source(fuchsia::time::UtcSource::UNVERIFIED);
  utc_state.set_timestamp(1234);

  EXPECT_CALL(utc_impl_, MockWatchState).Times(1).WillOnce(Return(&utc_state));

  bool called = false;
  clock_->AwaitExternalSource([&called]() { called = true; });

  RunLoopUntilIdle();

  EXPECT_TRUE(called);
}

TEST_F(FuchsiaSystemClockTest, AwaitExternalSourceInitiallyAccurate) {
  fuchsia::time::UtcState utc_state;
  utc_state.set_source(fuchsia::time::UtcSource::EXTERNAL);
  utc_state.set_timestamp(1234);

  EXPECT_CALL(utc_impl_, MockWatchState).Times(1).WillOnce(Return(&utc_state));

  bool called = false;
  clock_->AwaitExternalSource([&called]() { called = true; });

  RunLoopUntilIdle();

  EXPECT_TRUE(called);
}

TEST_F(FuchsiaSystemClockTest, AwaitExternalSourceNotInitiallyAccurate) {
  fuchsia::time::UtcState utc_state_backstop;
  utc_state_backstop.set_source(fuchsia::time::UtcSource::BACKSTOP);
  utc_state_backstop.set_timestamp(1234);
  fuchsia::time::UtcState utc_state_external;
  utc_state_external.set_source(fuchsia::time::UtcSource::EXTERNAL);
  utc_state_external.set_timestamp(1235);

  EXPECT_CALL(utc_impl_, MockWatchState)
      .Times(2)
      .WillOnce(Return(&utc_state_backstop))
      .WillOnce(Return(&utc_state_external));

  bool called = false;
  clock_->AwaitExternalSource([&called]() { called = true; });

  RunLoopUntilIdle();

  EXPECT_TRUE(called);
}

TEST_F(FuchsiaSystemClockTest, NowBeforeInitialized) { EXPECT_EQ(clock_->now(), std::nullopt); }

TEST_F(FuchsiaSystemClockTest, NowAfterInitialized) {
  fuchsia::time::UtcState utc_state;
  utc_state.set_source(fuchsia::time::UtcSource::EXTERNAL);
  utc_state.set_timestamp(1234);

  EXPECT_CALL(utc_impl_, MockWatchState).WillRepeatedly(Return(&utc_state));

  clock_->AwaitExternalSource([]() {});

  RunLoopUntilIdle();

  EXPECT_NE(clock_->now(), std::nullopt);
}

// Tests our use of an atomic_bool. We can set |accurate_| true in
// one thread and read it as true in another thread.
TEST_F(FuchsiaSystemClockTest, NowFromAnotherThread) {
  fuchsia::time::UtcState utc_state;
  utc_state.set_source(fuchsia::time::UtcSource::EXTERNAL);
  utc_state.set_timestamp(1234);

  EXPECT_CALL(utc_impl_, MockWatchState).WillRepeatedly(Return(&utc_state));

  clock_->AwaitExternalSource([]() {});

  RunLoopUntilIdle();

  EXPECT_NE(std::async(std::launch::async, [this]() { return clock_->now(); }).get(), std::nullopt);
}

}  // namespace cobalt
