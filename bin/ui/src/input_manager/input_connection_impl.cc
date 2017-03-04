// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_manager/input_connection_impl.h"

#include "apps/mozart/services/views/cpp/formatting.h"
#include "apps/mozart/src/input_manager/input_associate.h"

namespace input_manager {

InputConnectionImpl::InputConnectionImpl(
    InputAssociate* associate,
    mozart::ViewTokenPtr view_token,
    fidl::InterfaceRequest<mozart::InputConnection> request)
    : associate_(associate),
      view_token_(std::move(view_token)),
      binding_(this, std::move(request)) {
  FTL_DCHECK(associate_);
  FTL_DCHECK(view_token_);
  binding_.set_connection_error_handler(
      [this] { associate_->OnInputConnectionDied(this); });
}

InputConnectionImpl::~InputConnectionImpl() {}

void InputConnectionImpl::DeliverEvent(mozart::InputEventPtr event,
                                       OnEventDelivered callback) {
  if (!event_listener_) {
    FTL_VLOG(1) << "DeliverEvent: " << *view_token_
                << " dropped because there was no listener";
    callback(false);
    return;
  }
  event_listener_->OnEvent(std::move(event), callback);
}

void InputConnectionImpl::HitTest(
    mozart::PointFPtr point,
    const mozart::ViewHitTester::HitTestCallback& callback) {
  if (!view_hit_listener_) {
    FTL_VLOG(1) << "ViewHitTest: " << *view_token_
                << " dropped because there was no listener";
    callback(true, nullptr);
    return;
  }
  view_hit_listener_->HitTest(std::move(point), callback);
}

void InputConnectionImpl::SetEventListener(
    fidl::InterfaceHandle<mozart::InputListener> listener) {
  event_listener_ = mozart::InputListenerPtr::Create(std::move(listener));
}

void InputConnectionImpl::SetViewHitTester(
    fidl::InterfaceHandle<mozart::ViewHitTester> listener) {
  view_hit_listener_ = mozart::ViewHitTesterPtr::Create(std::move(listener));
}

}  // namespace input_manager
