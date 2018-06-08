// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/default.h>
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
                     std::move(directory_request_server)),
      controller_(this),
      service_root_vfs_(async_get_default()),
      service_root_dir_(fbl::AdoptRef(new fs::PseudoDir())) {
  outgoing_public_services_.Bind(
      ChannelConnectAt(directory_request_client.get(), "public"));

  // TODO(CP-57): simplify this
  service_root_dir_->AddEntry(
      FakeLauncher::Name_,
      fbl::AdoptRef(new fs::Service([&](zx::channel channel) {
        fake_launcher_.Bind(
            fidl::InterfaceRequest<Launcher>(std::move(channel)));
        return ZX_OK;
      })));
  service_root_vfs_.ServeDirectory(service_root_dir_,
                                   std::move(service_root_server));
  incoming_services().ConnectToService(launcher_.NewRequest());
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

zx::channel StartupContextForTest::ChannelConnectAt(zx_handle_t root,
                                                    const char* path) {
  zx::channel client, server;
  FXL_CHECK(zx::channel::create(0, &client, &server) == ZX_OK);
  FXL_CHECK(fdio_service_connect_at(root, path, server.release()) == ZX_OK);
  return client;
}

}  // namespace testing
}  // namespace sys
}  // namespace fuchsia
