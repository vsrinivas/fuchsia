// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_manager_v2.h"

#include <lib/syslog/cpp/macros.h>

namespace a11y {
namespace {

using fuchsia::ui::pointer::TouchResponse;
using fuchsia::ui::pointer::TouchResponseType;
using fuchsia::ui::pointer::augment::TouchEventWithLocalHit;
using fuchsia::ui::pointer::augment::TouchSourceWithLocalHitPtr;

}  // namespace

GestureManagerV2::GestureManagerV2(TouchSourceWithLocalHitPtr touch_source)
    : touch_source_(std::move(touch_source)) {
  WatchForTouchEvents({});
}

void GestureManagerV2::WatchForTouchEvents(std::vector<TouchResponse> old_responses) {
  touch_source_->Watch(std::move(old_responses), [this](
                                                     std::vector<TouchEventWithLocalHit> events) {
    std::vector<TouchResponse> responses(events.size());

    for (uint32_t i = 0; i < events.size(); ++i) {
      // Save device info and view parameters, if any.
      if (events[i].touch_event.has_device_info() && events[i].touch_event.device_info().has_id()) {
        FX_DCHECK(!touch_device_id_.has_value());
        touch_device_id_ = events[i].touch_event.device_info().id();
      }
      if (events[i].touch_event.has_view_parameters()) {
        view_parameters_ = events[i].touch_event.view_parameters();
      }

      if (events[i].touch_event.has_pointer_sample()) {
        // For now, simply accept all touch events.
        responses[i].set_response_type(TouchResponseType::YES);

        if (events[i].touch_event.has_trace_flow_id()) {
          responses[i].set_trace_flow_id(events[i].touch_event.trace_flow_id());
        }
      } else {
        // For events other than touch events, send an empty response.
        FX_DCHECK(responses[i].IsEmpty());
      }
    }

    WatchForTouchEvents(std::move(responses));
  });
}

}  // namespace a11y
