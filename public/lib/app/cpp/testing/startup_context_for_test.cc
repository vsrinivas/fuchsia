// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/testing/startup_context_for_test.h"

#include <lib/async/default.h>
#include <lib/fdio/util.h>

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
  zx_status_t status = service_root_dir_->AddEntry(
      FakeLauncher::Name_,
      fbl::AdoptRef(new fs::Service([&](zx::channel channel) {
        fake_launcher_.Bind(
            fidl::InterfaceRequest<Launcher>(std::move(channel)));
        return ZX_OK;
      })));
  ZX_ASSERT(status == ZX_OK);

  status = service_root_vfs_.ServeDirectory(service_root_dir_,
                                            std::move(service_root_server));
  ZX_ASSERT(status == ZX_OK);

  incoming_services().ConnectToService(launcher_.NewRequest());
}

std::unique_ptr<StartupContextForTest> StartupContextForTest::Create() {
  // TODO(CP-46): implement /svc instrumentation
  zx::channel service_root_client, service_root_server;
  zx_status_t status =
      zx::channel::create(0, &service_root_client, &service_root_server);
  ZX_ASSERT(status == ZX_OK);

  zx::channel directory_request_client, directory_request_server;
  status = zx::channel::create(0, &directory_request_client,
                               &directory_request_server);
  ZX_ASSERT(status == ZX_OK);

  return std::make_unique<StartupContextForTest>(
      std::move(service_root_client), std::move(service_root_server),
      std::move(directory_request_client), std::move(directory_request_server));
}

zx::channel StartupContextForTest::ChannelConnectAt(zx_handle_t root,
                                                    const char* path) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ZX_ASSERT(status == ZX_OK);

  status = fdio_service_connect_at(root, path, server.release());
  ZX_ASSERT(status == ZX_OK);

  return client;
}

}  // namespace testing
}  // namespace sys
}  // namespace fuchsia
