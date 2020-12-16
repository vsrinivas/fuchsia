// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/inspect.h"

#include <lib/async/cpp/time.h>
#include <lib/async/default.h>
#include <lib/async/dispatcher.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

namespace root_presenter {

namespace {

// A histogram that ranges from 1ms to ~8s.
constexpr zx::duration kLatencyHistogramFloor = zx::msec(1);
constexpr zx::duration kLatencyHistogramInitialStep = zx::msec(1);
constexpr uint64_t kLatencyHistogramStepMultiplier = 2;
constexpr size_t kLatencyHistogramBuckets = 14;

}  // namespace

InputReportInspector::InputReportInspector(inspect::Node node)
    : node_(std::move(node)),
      media_buttons_(node_.CreateExponentialUintHistogram(
          "media_buttons_latency", kLatencyHistogramFloor.to_nsecs(),
          kLatencyHistogramInitialStep.to_nsecs(), kLatencyHistogramStepMultiplier,
          kLatencyHistogramBuckets)),
      touchscreen_(node_.CreateExponentialUintHistogram(
          "touchscreen_latency", kLatencyHistogramFloor.to_nsecs(),
          kLatencyHistogramInitialStep.to_nsecs(), kLatencyHistogramStepMultiplier,
          kLatencyHistogramBuckets)) {}

void InputReportInspector::OnInputReport(const fuchsia::ui::input::InputReport& report) {
  async_dispatcher_t* dispatcher = async_get_default_dispatcher();
  if (dispatcher == nullptr) {
    FX_LOGS(ERROR) << "InputReport dropped from inspect metrics. async_get_default_dispatcher() "
                      "returned null.";
    return;
  }

  zx::duration latency = async::Now(dispatcher) - zx::time(report.event_time);

  if (report.media_buttons) {
    media_buttons_.Insert(latency.to_nsecs());
  } else if (report.touchscreen) {
    touchscreen_.Insert(latency.to_nsecs());
  } else {
    FX_LOGS(ERROR) << "InputReport dropped from inspect metrics. Unexpected InputReport type.";
  }
}

InputEventInspector::InputEventInspector(inspect::Node node)
    : node_(std::move(node)),
      pointer_(node_.CreateExponentialUintHistogram(
          "pointer_latency", kLatencyHistogramFloor.to_nsecs(),
          kLatencyHistogramInitialStep.to_nsecs(), kLatencyHistogramStepMultiplier,
          kLatencyHistogramBuckets)) {}

void InputEventInspector::OnInputEvent(const fuchsia::ui::input::InputEvent& event) {
  async_dispatcher_t* dispatcher = async_get_default_dispatcher();
  if (dispatcher == nullptr) {
    FX_LOGS(ERROR)
        << "InputEvent dropped from inspect metrics. async_get_default_dispatcher() returned null.";
    return;
  }

  if (event.is_pointer()) {
    zx::duration latency = async::Now(dispatcher) - zx::time(event.pointer().event_time);
    pointer_.Insert(latency.to_nsecs());
  } else {
    FX_LOGS(ERROR) << "InputEvent dropped from inspect metrics. Unexpected InputEvent type.";
  }
}

}  // namespace root_presenter
