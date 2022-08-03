// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/touch_source_with_local_hit.h"

#include <lib/syslog/cpp/macros.h>

namespace scenic_impl::input {

TouchSourceWithLocalHit::TouchSourceWithLocalHit(
    zx_koid_t view_ref_koid,
    fidl::InterfaceRequest<fuchsia::ui::pointer::augment::TouchSourceWithLocalHit> request,
    fit::function<void(StreamId, const std::vector<GestureResponse>&)> respond,
    fit::function<void()> error_handler,
    fit::function<std::pair<zx_koid_t, std::array<float, 2>>(const InternalTouchEvent&)>
        get_local_hit,
    GestureContenderInspector& inspector)
    : TouchSourceBase(
          utils::ExtractKoid(request.channel()), view_ref_koid, std::move(respond),
          [this](zx_status_t epitaph) { CloseChannel(epitaph); },
          /*augment*/
          [this](AugmentedTouchEvent& out_event, const InternalTouchEvent& in_event) {
            const auto [view_ref_koid, local_point] = get_local_hit_(in_event);
            out_event.local_hit = {
                .local_viewref_koid = view_ref_koid,
                .local_point = local_point,
            };
          },
          inspector),
      binding_(this, std::move(request)),
      error_handler_(std::move(error_handler)),
      get_local_hit_(std::move(get_local_hit)) {
  binding_.set_error_handler([this](zx_status_t) { error_handler_(); });
}

void TouchSourceWithLocalHit::Watch(std::vector<fuchsia::ui::pointer::TouchResponse> responses,
                                    WatchCallback callback) {
  TouchSourceBase::WatchBase(std::move(responses), [callback = std::move(callback)](
                                                       std::vector<AugmentedTouchEvent> events) {
    std::vector<fuchsia::ui::pointer::augment::TouchEventWithLocalHit> out_events;
    out_events.reserve(events.size());
    for (auto& event : events) {
      FX_DCHECK(event.local_hit.has_value());

      out_events.emplace_back(fuchsia::ui::pointer::augment::TouchEventWithLocalHit{
          .touch_event = std::move(event.touch_event),
          .local_viewref_koid = event.local_hit->local_viewref_koid,
          .local_point = event.local_hit->local_point,
      });
    }
    callback(std::move(out_events));
  });
}

void TouchSourceWithLocalHit::CloseChannel(zx_status_t epitaph) {
  binding_.Close(epitaph);
  // NOTE: Triggers destruction of this object.
  error_handler_();
}

}  // namespace scenic_impl::input
