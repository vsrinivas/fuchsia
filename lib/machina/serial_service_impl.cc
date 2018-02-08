// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/serial_service_impl.h"

#include <zx/handle.h>

#include "garnet/lib/machina/fidl/serial.fidl.h"
#include "lib/fxl/logging.h"

namespace machina {

SerialServiceImpl::SerialServiceImpl(
    app::ApplicationContext* application_context) {
  zx_status_t status = zx::socket::create(0, &socket_, &client_socket_);
  FXL_CHECK(status == ZX_OK) << "Failed to create socket for serial service";

  application_context->outgoing_services()->AddService<SerialService>(
      [this](fidl::InterfaceRequest<SerialService> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

void SerialServiceImpl::Connect(const ConnectCallback& callback) {
  callback(std::move(client_socket_));
}

}  // namespace machina
