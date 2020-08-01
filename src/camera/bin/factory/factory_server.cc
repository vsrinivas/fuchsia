// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "factory_server.h"

#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>

#include <fbl/unique_fd.h>
#include <src/lib/files/file.h>

namespace camera {

fit::result<std::unique_ptr<FactoryServer>, zx_status_t> FactoryServer::Create() {
  auto server = std::unique_ptr<FactoryServer>();
  return fit::ok(std::move(server));
}

}  // namespace camera
