// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_manager_v2.h"

#include <fuchsia/ui/pointer/augment/cpp/fidl.h>
#include <fuchsia/ui/pointer/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/syslog/cpp/macros.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace accessibility_test {
namespace {

using a11y::GestureManagerV2;
using fidl::Binding;
using fidl::InterfaceRequest;
using fuchsia::ui::pointer::TouchDeviceInfo;
using fuchsia::ui::pointer::TouchEvent;
using fuchsia::ui::pointer::TouchInteractionId;
using fuchsia::ui::pointer::TouchPointerSample;
using fuchsia::ui::pointer::TouchResponse;
using fuchsia::ui::pointer::TouchResponseType;
using fuchsia::ui::pointer::augment::TouchEventWithLocalHit;
using fuchsia::ui::pointer::augment::TouchSourceWithLocalHit;
using fuchsia::ui::pointer::augment::TouchSourceWithLocalHitPtr;

TouchEventWithLocalHit mock_touch_event() {
  TouchEvent inner;
  inner.set_pointer_sample({});
  inner.set_trace_flow_id(0);

  return {std::move(inner), 0, {0, 0}};
}

std::vector<TouchEventWithLocalHit> n_events(uint64_t n) {
  std::vector<TouchEventWithLocalHit> events(n);
  for (uint32_t i = 0; i < n; ++i) {
    events[i] = mock_touch_event();
  }
  return events;
}

class MockTouchSource : public TouchSourceWithLocalHit {
 public:
  explicit MockTouchSource(InterfaceRequest<TouchSourceWithLocalHit> server_end)
      : connected_client_(this, std::move(server_end)) {}

  // |fuchsia::ui::pointer::augment::TouchSourceWithLocalHit|
  void Watch(std::vector<TouchResponse> responses, WatchCallback callback) override {
    ++num_watch_calls_;
    responses_ = std::move(responses);
    callback_ = std::move(callback);
  }

  // |fuchsia::ui::pointer::augment::TouchSourceWithLocalHit|
  void UpdateResponse(TouchInteractionId interaction, TouchResponse response,
                      UpdateResponseCallback callback) override {
    FAIL() << "unimplemented";
  }

  uint32_t NumWatchCalls() const { return num_watch_calls_; }

  void SimulateEvents(std::vector<TouchEventWithLocalHit> events) {
    FX_CHECK(callback_);
    callback_(std::move(events));
    callback_ = nullptr;
  }

  std::vector<TouchResponse> TakeResponses() { return std::move(responses_); }

 private:
  uint32_t num_watch_calls_ = 0;
  std::vector<TouchResponse> responses_;
  WatchCallback callback_;

  Binding<TouchSourceWithLocalHit> connected_client_;
};

class GestureManagerV2Test : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    gtest::TestLoopFixture::SetUp();

    TouchSourceWithLocalHitPtr client_end;
    auto server_end = client_end.NewRequest();
    mock_touch_source_ = std::make_unique<MockTouchSource>(std::move(server_end));
    gesture_manager_ = std::make_unique<GestureManagerV2>(std::move(client_end));
  }

 protected:
  std::unique_ptr<MockTouchSource> mock_touch_source_;
  std::unique_ptr<GestureManagerV2> gesture_manager_;
};

TEST_F(GestureManagerV2Test, RespondYesToTouchEvents) {
  // Gesture manager should call `Watch` in its constructor.
  RunLoopUntilIdle();
  EXPECT_EQ(mock_touch_source_->NumWatchCalls(), 1u);

  for (const uint32_t n : {3, 0, 1}) {
    auto events = n_events(n);
    mock_touch_source_->SimulateEvents(std::move(events));

    RunLoopUntilIdle();
    auto responses = mock_touch_source_->TakeResponses();

    EXPECT_EQ(responses.size(), n);
    for (uint32_t i = 0; i < n; ++i) {
      EXPECT_EQ(responses[i].response_type(), TouchResponseType::YES);
      EXPECT_TRUE(responses[i].has_trace_flow_id());
    }
  }
}

}  // namespace
}  // namespace accessibility_test
