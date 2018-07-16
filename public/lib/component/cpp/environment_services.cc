// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/component/cpp/environment_services.h"

#include <lib/fdio/util.h>

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

namespace subtle {

// static
zx::channel CreateStaticServiceRootHandle() {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) != ZX_OK) return zx::channel();
  // TODO(abarth): Use kServiceRootPath once that actually works.
  if (fdio_service_connect("/svc/.", h1.release()) != ZX_OK)
    return zx::channel();
  return h2;
}

}  // namespace subtle

}  // namespace component
