// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_TOUCH_SOURCE_H_
#define SRC_UI_SCENIC_LIB_INPUT_TOUCH_SOURCE_H_

#include <fidl/fuchsia.ui.pointer/cpp/fidl.h>
#include <fidl/fuchsia.ui.pointer/cpp/hlcpp_conversion.h>
#include <fuchsia/ui/pointer/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include "src/lib/fxl/macros.h"
#include "src/ui/scenic/lib/input/touch_source_base.h"

namespace scenic_impl::input {

// Implementation of the |fidl::Server<fuchsia_ui_pointer::TouchSource>| interface. One instance per
// channel.
class TouchSource : public TouchSourceBase, public fidl::Server<fuchsia_ui_pointer::TouchSource> {
 public:
  // |respond_| must not destroy the TouchSource object.
  TouchSource(zx_koid_t view_ref_koid,
              fidl::ServerEnd<fuchsia_ui_pointer::TouchSource> touch_source,
              fit::function<void(StreamId, const std::vector<GestureResponse>&)> respond,
              fit::function<void()> error_handler, GestureContenderInspector& inspector);

  ~TouchSource() override = default;

  // |fidl::Server<fuchsia_ui_pointer::TouchSource>|
  void Watch(WatchRequest& request, WatchCompleter::Sync& completer) override {
    std::vector<fuchsia_ui_pointer::TouchResponse>& responses = request.responses();
    TouchSourceBase::WatchBase(
        fidl::NaturalToHLCPP(std::move(responses)),
        [completer = completer.ToAsync()](std::vector<AugmentedTouchEvent> events) mutable {
          std::vector<fuchsia_ui_pointer::TouchEvent> out_events;
          out_events.reserve(events.size());
          for (auto& event : events) {
            out_events.emplace_back(fidl::HLCPPToNatural(std::move(event.touch_event)));
          }
          completer.Reply(std::move(out_events));
        });
  }

  // |fidl::Server<fuchsia_ui_pointer::TouchSource>|
  void UpdateResponse(UpdateResponseRequest& request,
                      UpdateResponseCompleter::Sync& completer) override {
    fuchsia_ui_pointer::TouchInteractionId& stream = request.interaction();
    fuchsia_ui_pointer::TouchResponse& response = request.response();
    TouchSourceBase::UpdateResponseBase(
        fidl::NaturalToHLCPP(stream), fidl::NaturalToHLCPP(std::move(response)),
        [completer = completer.ToAsync()]() mutable { completer.Reply(); });
  }

 private:
  void CloseChannel(zx_status_t epitaph);

  fidl::ServerBinding<fuchsia_ui_pointer::TouchSource> binding_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_TOUCH_SOURCE_H_
