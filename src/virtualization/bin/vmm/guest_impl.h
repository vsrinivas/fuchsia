// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_GUEST_IMPL_H_
#define SRC_VIRTUALIZATION_BIN_VMM_GUEST_IMPL_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

// Provides an implementation of the |fuchsia::virtualization::Guest|
// interface. This exposes some guest services over FIDL.
class GuestImpl : public fuchsia::virtualization::Guest {
 public:
  GuestImpl();

  zx_status_t AddPublicService(sys::ComponentContext* context);

  // Return the host side of the guest's console socket.
  //
  // The other end of this socket will be provided to clients via |GetConsole|.
  zx::socket ConsoleSocket();

  // Return the host side of the guest's low-level serial socket.
  //
  // The other end of this socket will be provided to clients via |GetSerial|.
  zx::socket SerialSocket();

  // |fuchsia::virtualization::Guest|
  void GetSerial(GetSerialCallback callback) override;
  void GetConsole(GetConsoleCallback callback) override;

 private:
  fidl::BindingSet<fuchsia::virtualization::Guest> bindings_;

  // Host/client end of the serial socket.
  zx::socket serial_socket_;
  zx::socket remote_serial_socket_;

  // Host/client end of the console socket.
  zx::socket console_socket_;
  zx::socket remote_console_socket_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_GUEST_IMPL_H_
