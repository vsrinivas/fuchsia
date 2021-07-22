// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_MOUSE_INJECTOR_H_
#define SRC_UI_SCENIC_LIB_INPUT_MOUSE_INJECTOR_H_

#include "src/ui/scenic/lib/input/injector.h"
#include "src/ui/scenic/lib/input/internal_pointer_event.h"

namespace scenic_impl::input {

// Implementation of the |fuchsia::ui::pointerinjector::Device| interface. One instance per channel.
class MouseInjector : public Injector {
 public:
  MouseInjector(inspect::Node inspect_node, InjectorSettings settings, Viewport viewport,
                fidl::InterfaceRequest<fuchsia::ui::pointerinjector::Device> device,
                fit::function<bool(/*descendant*/ zx_koid_t, /*ancestor*/ zx_koid_t)>
                    is_descendant_and_connected,
                fit::function<void(const InternalMouseEvent&, StreamId stream_id)> inject,
                fit::function<void(StreamId stream_id)> cancel_stream,
                fit::function<void()> on_channel_closed);

 protected:
  // |Injector|
  void ForwardEvent(const fuchsia::ui::pointerinjector::Event& event, StreamId stream_id) override;
  // |Injector|
  void CancelStream(uint32_t pointer_id, StreamId stream_id) override;

 private:
  InternalMouseEvent PointerInjectorEventToInternalMouseEvent(
      const fuchsia::ui::pointerinjector::Event& event) const;

  // Used to inject the event into InputSystem for dispatch to clients.
  const fit::function<void(const InternalMouseEvent&, StreamId)> inject_;
  // Explicit call necessary to cancel mouse stream, because mouse stream itself does not track
  // phase.
  const fit::function<void(StreamId)> cancel_stream_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_MOUSE_INJECTOR_H_
