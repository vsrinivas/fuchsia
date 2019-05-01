// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_INSTANCE_CONTROLLER_IMPL_H_
#define SRC_VIRTUALIZATION_BIN_VMM_INSTANCE_CONTROLLER_IMPL_H_

#include <fuchsia/guest/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>

// Provides an implementation of the |fuchsia::guest::InstanceController|
// interface. This exposes some guest services over FIDL.
class InstanceControllerImpl : public fuchsia::guest::InstanceController {
 public:
  InstanceControllerImpl();

  zx_status_t AddPublicService(component::StartupContext* context);

  // Extracts the socket handle to be used for the host end of serial
  // communication. The other end of this socket will be provided to clients
  // via |GetSerial|.
  zx::socket SerialSocket();

  // |fuchsia::guest::InstanceController|
  void GetSerial(GetSerialCallback callback) override;

 private:
  fidl::BindingSet<fuchsia::guest::InstanceController> bindings_;

  zx::socket socket_;
  zx::socket remote_socket_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_INSTANCE_CONTROLLER_IMPL_H_
