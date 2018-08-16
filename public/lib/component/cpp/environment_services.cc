// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/component/cpp/environment_services.h"

#include <lib/fdio/util.h>

#include "lib/component/cpp/startup_context.h"

namespace component {

// static
void ConnectToEnvironmentService(const std::string& interface_name,
                                 zx::channel channel) {
  static zx_handle_t service_root = []() {
    zx::channel service_root = subtle::CreateStaticServiceRootHandle();
    return service_root.release();
  }();
  fdio_service_connect_at(service_root, interface_name.c_str(),
                          channel.release());
}

}  // namespace component
