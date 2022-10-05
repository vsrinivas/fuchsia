// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_TOUCH_INJECTOR_H_
#define SRC_UI_SCENIC_LIB_INPUT_TOUCH_INJECTOR_H_

#include "src/ui/scenic/lib/input/injector.h"

namespace scenic_impl::input {

// Implementation of the |fuchsia::ui::pointerinjector::Device| interface. One instance per channel.
class TouchInjector : public Injector {
 public:
  TouchInjector(inspect::Node inspect_node, InjectorSettings settings, Viewport viewport,
                fidl::InterfaceRequest<fuchsia::ui::pointerinjector::Device> device,
                fit::function<bool(/*descendant*/ zx_koid_t, /*ancestor*/ zx_koid_t)>
                    is_descendant_and_connected,
                fit::function<void(const InternalTouchEvent&, StreamId stream_id)> inject,
                fit::function<void()> on_channel_closed);

 protected:
  // |Injector|
  void ForwardEvent(const fuchsia::ui::pointerinjector::Event& event, StreamId stream_id) override;
  // |Injector|
  void CancelStream(uint32_t pointer_id, StreamId stream_id) override;

 private:
  InternalTouchEvent PointerInjectorEventToInternalTouchEvent(
      const fuchsia::ui::pointerinjector::Event& event) const;

  // Used to inject the event into InputSystem for dispatch to clients.
  const fit::function<void(const InternalTouchEvent&, StreamId)> inject_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_TOUCH_INJECTOR_H_
