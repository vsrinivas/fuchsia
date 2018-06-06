// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/util.h>
#include <lib/fxl/logging.h>

#include "startup_context_for_test.h"

namespace fuchsia {
namespace sys {
namespace testing {

StartupContextForTest::StartupContextForTest(
    zx::channel service_root_client, zx::channel service_root_server,
    zx::channel directory_request_client, zx::channel directory_request_server)
    : StartupContext(std::move(service_root_client),
                     std::move(directory_request_server)) {
  zx::channel public_dir_client, public_dir_server;
  FXL_CHECK(zx::channel::create(0, &public_dir_client, &public_dir_server) ==
            ZX_OK);
  FXL_CHECK(fdio_service_connect_at(directory_request_client.get(), "public",
                                    public_dir_server.release()) == ZX_OK);
  services_.Bind(std::move(public_dir_client));
}

std::unique_ptr<StartupContextForTest> StartupContextForTest::Create() {
  // TODO(CP-46): implement /svc instrumentation
  zx::channel service_root_client, service_root_server;
  zx::channel::create(0, &service_root_client, &service_root_server);
  zx::channel directory_request_client, directory_request_server;
  zx::channel::create(0, &directory_request_client, &directory_request_server);
  return std::make_unique<StartupContextForTest>(
      std::move(service_root_client), std::move(service_root_server),
      std::move(directory_request_client), std::move(directory_request_server));
}

}  // namespace testing
}  // namespace sys
}  // namespace fuchsia
