// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/input_manager/input_connection_impl.h"

#include "base/bind.h"
#include "base/logging.h"
#include "services/ui/input_manager/input_associate.h"

namespace input_manager {

InputConnectionImpl::InputConnectionImpl(
    InputAssociate* associate,
    mojo::ui::ViewTokenPtr view_token,
    mojo::InterfaceRequest<mojo::ui::InputConnection> request)
    : associate_(associate),
      view_token_(view_token.Pass()),
      binding_(this, request.Pass()) {
  DCHECK(associate_);
  DCHECK(view_token_);
  binding_.set_connection_error_handler(
      base::Bind(&InputAssociate::OnInputConnectionDied,
                 base::Unretained(associate_), base::Unretained(this)));
}

InputConnectionImpl::~InputConnectionImpl() {}

void InputConnectionImpl::DeliverEvent(mojo::EventPtr event) {
  // TODO(jeffbrown): Pass the result back up the stack and handle errors.
  if (!listener_) {
    DVLOG(1) << "DeliverEvent: dropped because there was no listener";
    return;
  }

  listener_->OnEvent(event.Pass(),
                     base::Bind(&InputConnectionImpl::OnEventFinished,
                                base::Unretained(this)));
}

void InputConnectionImpl::OnEventFinished(bool handled) {
  // TODO: this code doesn't really belong here
}

void InputConnectionImpl::SetListener(
    mojo::InterfaceHandle<mojo::ui::InputListener> listener) {
  listener_ = mojo::ui::InputListenerPtr::Create(std::move(listener));
}

}  // namespace input_manager
