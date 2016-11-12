// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_manager/input_connection_impl.h"

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

void InputConnectionImpl::DeliverEvent(mozart::EventPtr event) {
  // TODO(jeffbrown): Pass the result back up the stack and handle errors.
  if (!listener_) {
    FTL_VLOG(1) << "DeliverEvent: dropped because there was no listener";
    return;
  }

  listener_->OnEvent(std::move(event),
                     [this](bool handled) { OnEventFinished(handled); });
}

void InputConnectionImpl::OnEventFinished(bool handled) {
  // TODO: this code doesn't really belong here
}

void InputConnectionImpl::SetListener(
    fidl::InterfaceHandle<mozart::InputListener> listener) {
  listener_ = mozart::InputListenerPtr::Create(std::move(listener));
}

}  // namespace input_manager
