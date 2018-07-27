// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_A11Y_TOUCH_DISPATCHER_A11Y_TOUCH_DISPATCHER_IMPL_H_
#define GARNET_BIN_A11Y_A11Y_TOUCH_DISPATCHER_A11Y_TOUCH_DISPATCHER_IMPL_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace a11y_touch_dispatcher {

// Routes raw input events from one active Presentation to one
// gesture detection client, and simulates inputs from the client back to
// the presentation.
class A11yTouchDispatcherImpl : public fuchsia::accessibility::TouchDispatcher,
                                public fuchsia::accessibility::InputReceiver {
 public:
  A11yTouchDispatcherImpl();
  ~A11yTouchDispatcherImpl() = default;

  // If a client is already connected to this object's InputReceiver binding,
  // the old client is disconnected and the new client is connected. The
  // client must first call |RegisterPresentation| and send over its
  // ViewTreeToken for |presentation_valid_| to be true.
  void BindInputReceiver(
      fidl::InterfaceRequest<fuchsia::accessibility::InputReceiver> request);

  // If a client is already connected to this object's TouchDispatcher binding,
  // the old client is disconnected and the new client is connected.
  void BindTouchDispatcher(
      fidl::InterfaceRequest<fuchsia::accessibility::TouchDispatcher> request);

 private:
  // |fuchsia::accessibility::TouchDispatcher|
  void SendSimulatedPointerEvent(
      fuchsia::ui::input::PointerEvent event) override;

  // |fuchsia::accessibility::InputReceiver|
  // Registers that a valid presentation with id |tree_token| is connected.
  // If there is a TouchDispatcher client connected, signal to it that a
  // new presentation with id |tree_token| has been made active.
  void RegisterPresentation(
      fuchsia::ui::viewsv1::ViewTreeToken tree_token) override;
  // If the current presentation is valid, the input event is a touch pointer,
  // and a TouchDispatcher client is connected, dispatch the pointer event
  // to the TouchDispatcher client.
  void SendInputEvent(fuchsia::ui::input::InputEvent event) override;

  // Returns a clone of the current view tree token. |valid_presentation_|
  // must be true.
  fuchsia::ui::viewsv1::ViewTreeToken GetViewTreeToken();

  fidl::Binding<fuchsia::accessibility::InputReceiver> input_receiver_binding_;
  fidl::Binding<fuchsia::accessibility::TouchDispatcher>
      touch_dispatcher_binding_;

  // A flag to signify whether a presentation is currently connected and
  // registered.
  bool presentation_valid_;
  // The ViewTreeToken of the currently connected and registered presentation.
  // Valid only if |presentation_valid_| is true.
  fuchsia::ui::viewsv1::ViewTreeToken tree_token_;

  FXL_DISALLOW_COPY_AND_ASSIGN(A11yTouchDispatcherImpl);
};

}  // namespace a11y_touch_dispatcher

#endif  // GARNET_BIN_A11Y_A11Y_TOUCH_DISPATCHER_A11Y_TOUCH_DISPATCHER_IMPL_H_
