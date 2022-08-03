// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_TOUCH_SOURCE_WITH_LOCAL_HIT_H_
#define SRC_UI_SCENIC_LIB_INPUT_TOUCH_SOURCE_WITH_LOCAL_HIT_H_

#include <fuchsia/ui/pointer/augment/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include "src/lib/fxl/macros.h"
#include "src/ui/scenic/lib/input/touch_source_base.h"

namespace scenic_impl::input {

// Implementation of the |fuchsia::ui::pointer::augment::TouchSourceWithLocalHit| interface. One
// instance per channel.
class TouchSourceWithLocalHit : public TouchSourceBase,
                                public fuchsia::ui::pointer::augment::TouchSourceWithLocalHit {
 public:
  // |respond| must not destroy the TouchSourceWithLocalHit object.
  TouchSourceWithLocalHit(
      zx_koid_t view_ref_koid,
      fidl::InterfaceRequest<fuchsia::ui::pointer::augment::TouchSourceWithLocalHit> request,
      fit::function<void(StreamId, const std::vector<GestureResponse>&)> respond,
      fit::function<void()> error_handler,
      fit::function<std::pair<zx_koid_t, std::array<float, 2>>(const InternalTouchEvent&)>
          get_local_hit,
      GestureContenderInspector& inspector);

  ~TouchSourceWithLocalHit() override = default;

  // |fuchsia::ui::pointer::augment::TouchSourceWithLocalHit|
  void Watch(std::vector<fuchsia::ui::pointer::TouchResponse> responses,
             WatchCallback callback) override;

  // |fuchsia::ui::pointer::augment::TouchSourceWithLocalHit|
  void UpdateResponse(fuchsia::ui::pointer::TouchInteractionId stream,
                      fuchsia::ui::pointer::TouchResponse response,
                      UpdateResponseCallback callback) override {
    TouchSourceBase::UpdateResponseBase(stream, std::move(response), std::move(callback));
  }

 private:
  void CloseChannel(zx_status_t epitaph);

  fidl::Binding<fuchsia::ui::pointer::augment::TouchSourceWithLocalHit> binding_;
  const fit::function<void()> error_handler_;
  const fit::function<std::pair<zx_koid_t, std::array<float, 2>>(const InternalTouchEvent&)>
      get_local_hit_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_TOUCH_SOURCE_WITH_LOCAL_HIT_H_
