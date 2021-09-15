// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_MOUSE_SOURCE_WITH_GLOBAL_MOUSE_H_
#define SRC_UI_SCENIC_LIB_INPUT_MOUSE_SOURCE_WITH_GLOBAL_MOUSE_H_

#include <fuchsia/ui/pointer/augment/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

#include "src/ui/scenic/lib/input/mouse_source_base.h"

namespace scenic_impl::input {

// Implementation of the |fuchsia::ui::pointer::MouseSource| interface. One instance per
// channel.
class MouseSourceWithGlobalMouse : public MouseSourceBase,
                                   fuchsia::ui::pointer::augment::MouseSourceWithGlobalMouse {
 public:
  MouseSourceWithGlobalMouse(
      fidl::InterfaceRequest<fuchsia::ui::pointer::augment::MouseSourceWithGlobalMouse>
          event_provider,
      fit::function<void()> error_handler);

  ~MouseSourceWithGlobalMouse() override = default;

  // |fuchsia::ui::pointer::MouseSourceWithGlobalMouse|
  void Watch(WatchCallback callback) override;

  // Called to send a global event. If the event should also include a |mouse_event| then
  // MouseSourceBase::UpdateStream() must have been called first.
  void AddGlobalEvent(const InternalMouseEvent& event, bool inside_view);

 private:
  // Recursively calls WatchBase() to collect all base events one by one.
  void CollectBaseEvents();

  void SendPendingIfWaiting();

  std::optional<fuchsia::ui::pointer::MouseEvent> last_base_event_;
  std::unordered_set<uint32_t> pointers_inside_view_;

  std::queue<fuchsia::ui::pointer::augment::MouseEventWithGlobalMouse> pending_events_;
  WatchCallback pending_callback_ = nullptr;

  fidl::Binding<fuchsia::ui::pointer::augment::MouseSourceWithGlobalMouse> binding_;
  const fit::function<void()> error_handler_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_MOUSE_SOURCE_WITH_GLOBAL_MOUSE_H_
