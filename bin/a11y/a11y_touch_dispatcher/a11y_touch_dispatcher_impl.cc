// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/a11y_touch_dispatcher/a11y_touch_dispatcher_impl.h"

namespace a11y_touch_dispatcher {

A11yTouchDispatcherImpl::A11yTouchDispatcherImpl()
    : input_receiver_binding_(this),
      touch_dispatcher_binding_(this),
      presentation_valid_(false) {
  input_receiver_binding_.set_error_handler([this] {
    FXL_LOG(INFO) << "Input receiver client disconnected";
    presentation_valid_ = false;
    input_receiver_binding_.Unbind();
  });
  touch_dispatcher_binding_.set_error_handler([this] {
    FXL_LOG(INFO) << "Touch dispatcher client disconnected.";
    touch_dispatcher_binding_.Unbind();
  });
};

void A11yTouchDispatcherImpl::BindInputReceiver(
    fidl::InterfaceRequest<fuchsia::accessibility::InputReceiver> request) {
  input_receiver_binding_.Bind(std::move(request));
  presentation_valid_ = false;
}

void A11yTouchDispatcherImpl::BindTouchDispatcher(
    fidl::InterfaceRequest<fuchsia::accessibility::TouchDispatcher> request) {
  touch_dispatcher_binding_.Bind(std::move(request));
  if (presentation_valid_) {
    touch_dispatcher_binding_.events().OnPresentationChangedEvent(
        GetViewTreeToken());
  }
}

void A11yTouchDispatcherImpl::SendSimulatedPointerEvent(
    fuchsia::ui::input::PointerEvent event) {
  if (!presentation_valid_) {
    return;
  }
  fuchsia::ui::input::InputEvent clone_event;
  clone_event.set_pointer(std::move(event));
  input_receiver_binding_.events().OnReturnInputEvent(std::move(clone_event));
}

void A11yTouchDispatcherImpl::RegisterPresentation(
    fuchsia::ui::viewsv1::ViewTreeToken tree_token) {
  FXL_LOG(INFO) << "Touch dispatcher registered with id: " << tree_token.value;
  tree_token_ = tree_token;
  presentation_valid_ = true;
  if (touch_dispatcher_binding_.is_bound()) {
    touch_dispatcher_binding_.events().OnPresentationChangedEvent(
        GetViewTreeToken());
  }
}

void A11yTouchDispatcherImpl::SendInputEvent(
    fuchsia::ui::input::InputEvent event) {
  if (presentation_valid_) {
    if (event.is_pointer() &&
        event.pointer().type == fuchsia::ui::input::PointerEventType::TOUCH &&
        touch_dispatcher_binding_.is_bound()) {
      touch_dispatcher_binding_.events().OnInputEvent(
          std::move(event.pointer()));
    } else {
      // Don't forward event; reroute back to main input dispatch.
      input_receiver_binding_.events().OnReturnInputEvent(std::move(event));
    }
  }
}

fuchsia::ui::viewsv1::ViewTreeToken
A11yTouchDispatcherImpl::GetViewTreeToken() {
  FXL_DCHECK(presentation_valid_);
  fuchsia::ui::viewsv1::ViewTreeToken clone_token;
  tree_token_.Clone(&clone_token);
  return clone_token;
}
}  // namespace a11y_touch_dispatcher