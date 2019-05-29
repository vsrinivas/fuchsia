// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_TEST_FIXTURE_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_TEST_FIXTURE_H_

#include <lib/fit/function.h>
#include <lib/gtest/real_loop_fixture.h>

#include <optional>

#include "src/lib/fxl/logging.h"

namespace media::audio::test {

// For operations expected to generate a response, wait __1 minute__. We do
// this to avoid flaky results when testing on high-load (high-latency)
// environments. For reference, in mid-2018 when observing highly-loaded local
// QEMU instances running code that correctly generated completion responses, we
// observed timeouts if waiting 20 ms, but not when waiting 50 ms. This value
// will be 3000x that (!), and is WELL beyond the limit of human acceptability.
// Thus, intermittent failures (rather than being a "potentially flaky test")
// mean that the system is, intermittently, UNACCEPTABLE.
//
// Also, when expecting a response we can save time by checking more frequently.
// Restated, kDurationResponseExpected should ALWAYS use kDurationGranularity.
//
// These two values codify the following ordered priorities:
//   1) False-positive test failures are expensive and must be eliminated.
//   2) Having done that, streamline test run-time (time=resources=cost);
constexpr zx::duration kDurationResponseExpected = zx::sec(60);
constexpr zx::duration kDurationGranularity = zx::msec(1);

constexpr char kDisconnectErr[] =
    "Connection to fuchsia.media FIDL interface was lost!\n";
constexpr char kTimeoutErr[] = "Timeout -- no callback received!\n";
constexpr char kCallbackErr[] = "Unexpected callback received!\n";

//
// TestFixture
//
class TestFixture : public ::gtest::RealLoopFixture {
 protected:
  void SetUp() override;
  void TearDown() override;

  // Simple handler, when the only required response is to record the error.
  auto ErrorHandler() {
    return [this](zx_status_t error) {
      error_occurred_ = true;
      error_code_ = error;
    };
  }

  // Accept (and call) a custom handler, for more nuanced error responses.
  template <typename Callable>
  auto ErrorHandler(Callable err_handler) {
    return [this, err_handler = std::move(err_handler)](zx_status_t error) {
      error_occurred_ = true;
      error_code_ = error;
      err_handler(error);
    };
  }

  // Simple callback, when the only requirement is to record the callback.
  auto CompletionCallback() {
    return [this]() { callback_received_ = true; };
  }

  // Accept (and call) a custom callback, for more nuanced behavior.
  template <typename Callable>
  auto CompletionCallback(Callable callback) {
    return [this, callback = std::move(callback)](auto&&... args) {
      callback_received_ = true;
      callback(std::forward<decltype(args)>(args)...);
    };
  }

  // The below methods contain gtest EXPECT checks that verify basic outcomes.
  //
  // Wait for CompletionCallback or ErrorHandler, expecting callback.
  virtual void ExpectCallback();

  // Wait for a more nuanced condition (not just callback_received_).
  virtual void ExpectCondition(fit::function<bool()> condition);

  // Wait for CompletionCallback or ErrorHandler, expecting the specified error.
  void ExpectError(zx_status_t expect_error);
  virtual void ExpectDisconnect() { ExpectError(ZX_ERR_PEER_CLOSED); }

  // Set expectations for negative test cases. Called by ExpectError/Disconnect.
  virtual void SetNegativeExpectations() { error_expected_ = true; }

  bool error_expected_ = false;
  bool error_occurred_ = false;
  zx_status_t error_code_ = ZX_OK;

 private:
  bool callback_received_ = false;

  // Run our asynchronous loop, until the provided condition is satisfied
  // (returns TRUE) or until a timeout occurs (returns FALSE).
  bool RunUntilComplete(fit::function<bool()> condition);
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_TEST_FIXTURE_H_
