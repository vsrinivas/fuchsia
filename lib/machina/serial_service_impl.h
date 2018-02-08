// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/socket.h>

#include "garnet/lib/machina/fidl/serial.fidl.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

namespace machina {

class SerialServiceImpl : public SerialService {
 public:
  SerialServiceImpl(app::ApplicationContext* application_context);

  // |SerialService|
  void Connect(const ConnectCallback& callback) override;

  zx_handle_t socket() const { return socket_.get(); }

 private:
  fidl::BindingSet<SerialService> bindings_;
  zx::socket socket_;
  zx::socket client_socket_;
};

}  // namespace machina
