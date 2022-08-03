// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_TOUCH_SOURCE_H_
#define SRC_UI_SCENIC_LIB_INPUT_TOUCH_SOURCE_H_

#include <fuchsia/ui/pointer/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include "src/lib/fxl/macros.h"
#include "src/ui/scenic/lib/input/touch_source_base.h"

namespace scenic_impl::input {

// Implementation of the |fuchsia::ui::pointer::TouchSource| interface. One instance per
// channel.
class TouchSource : public TouchSourceBase, public fuchsia::ui::pointer::TouchSource {
 public:
  // |respond_| must not destroy the TouchSource object.
  TouchSource(zx_koid_t view_ref_koid,
              fidl::InterfaceRequest<fuchsia::ui::pointer::TouchSource> touch_source,
              fit::function<void(StreamId, const std::vector<GestureResponse>&)> respond,
              fit::function<void()> error_handler, GestureContenderInspector& inspector);

  ~TouchSource() override = default;

  // |fuchsia::ui::pointer::TouchSource|
  void Watch(std::vector<fuchsia::ui::pointer::TouchResponse> responses,
             WatchCallback callback) override {
    TouchSourceBase::WatchBase(std::move(responses), [callback = std::move(callback)](
                                                         std::vector<AugmentedTouchEvent> events) {
      std::vector<fuchsia::ui::pointer::TouchEvent> out_events;
      out_events.reserve(events.size());
      for (auto& event : events) {
        out_events.emplace_back(std::move(event.touch_event));
      }
      callback(std::move(out_events));
    });
  }

  // |fuchsia::ui::pointer::TouchSource|
  void UpdateResponse(fuchsia::ui::pointer::TouchInteractionId stream,
                      fuchsia::ui::pointer::TouchResponse response,
                      UpdateResponseCallback callback) override {
    TouchSourceBase::UpdateResponseBase(stream, std::move(response), std::move(callback));
  }

 private:
  void CloseChannel(zx_status_t epitaph);

  fidl::Binding<fuchsia::ui::pointer::TouchSource> binding_;
  const fit::function<void()> error_handler_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_TOUCH_SOURCE_H_
