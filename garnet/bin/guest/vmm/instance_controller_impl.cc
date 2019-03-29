// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/instance_controller_impl.h"

#include <src/lib/fxl/logging.h>

static zx::socket duplicate(const zx::socket& socket) {
  zx::socket dup;
  zx_status_t status = socket.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
  FXL_CHECK(status == ZX_OK) << "Failed to duplicate socket " << status;
  return dup;
}

InstanceControllerImpl::InstanceControllerImpl() {
  zx_status_t status = zx::socket::create(0, &socket_, &remote_socket_);
  FXL_CHECK(status == ZX_OK) << "Failed to create socket";
}

zx_status_t InstanceControllerImpl::AddPublicService(
    component::StartupContext* context) {
  return context->outgoing().AddPublicService(bindings_.GetHandler(this));
}

zx::socket InstanceControllerImpl::SerialSocket() { return duplicate(socket_); }

void InstanceControllerImpl::GetSerial(GetSerialCallback callback) {
  callback(duplicate(remote_socket_));
}
