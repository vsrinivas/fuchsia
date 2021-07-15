// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/guest_impl.h"

#include <lib/syslog/cpp/macros.h>

static zx::socket duplicate(const zx::socket& socket) {
  zx::socket dup;
  zx_status_t status = socket.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
  FX_CHECK(status == ZX_OK) << "Failed to duplicate socket " << status;
  return dup;
}

GuestImpl::GuestImpl() {
  zx_status_t status = zx::socket::create(0, &serial_socket_, &remote_serial_socket_);
  FX_CHECK(status == ZX_OK) << "Failed to create serial socket";

  status = zx::socket::create(0, &console_socket_, &remote_console_socket_);
  FX_CHECK(status == ZX_OK) << "Failed to create console socket";
}

zx_status_t GuestImpl::AddPublicService(sys::ComponentContext* context) {
  return context->outgoing()->AddPublicService(bindings_.GetHandler(this));
}

zx::socket GuestImpl::SerialSocket() { return duplicate(serial_socket_); }

zx::socket GuestImpl::ConsoleSocket() { return duplicate(console_socket_); }

void GuestImpl::GetSerial(GetSerialCallback callback) {
  callback(duplicate(remote_serial_socket_));
}

void GuestImpl::GetConsole(GetConsoleCallback callback) {
  callback(fuchsia::virtualization::Guest_GetConsole_Result::WithResponse(
      fuchsia::virtualization::Guest_GetConsole_Response{duplicate(remote_console_socket_)}));
}
