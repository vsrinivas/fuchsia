// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.driver.test/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/service/llcpp/service.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include "sdk/lib/device-watcher/cpp/device-watcher.h"
#include "src/connectivity/network/testing/netemul/lib/network/network_context.h"

constexpr char kTapctlRelativePath[] = "sys/test/tapctl";

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  syslog::SetTags({"network-context"});
  FX_LOGS(INFO) << "starting...";

  std::unique_ptr context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  netemul::NetworkContext net_context;
  fdio_cpp::FdioCaller devfs_root;
  net_context.SetDevfsHandler([&devfs_root](zx::channel req) {
    // We need to start the driver test realm and then wait for
    // "/dev/sys/test/tapctl" to be available to ensure the driver is
    // initialized.
    static std::once_flag flag;
    std::call_once(flag, [&devfs_root]() {
      {
        zx::status client_end = service::Connect<fuchsia_driver_test::Realm>();
        if (client_end.is_error()) {
          FX_PLOGS(ERROR, client_end.status_value())
              << "failed to connect to "
              << fidl::DiscoverableProtocolName<fuchsia_driver_test::Realm>;
          return;
        }
        fidl::WireSyncClient client = fidl::BindSyncClient(std::move(client_end.value()));
        fidl::WireResult fidl_result = client->Start(fuchsia_driver_test::wire::RealmArgs());
        if (!fidl_result.ok()) {
          FX_LOGS(ERROR) << "failed to start driver test realm: " << fidl_result.error();
          return;
        }
        const fuchsia_driver_test::wire::RealmStartResult& realm_start_result =
            fidl_result.value().result;
        if (realm_start_result.is_err()) {
          FX_PLOGS(ERROR, realm_start_result.err()) << "driver test realm error";
          return;
        }
      }

      devfs_root.reset(fbl::unique_fd(open("/dev", O_RDONLY)));
      if (!devfs_root) {
        FX_LOGS(ERROR) << "failed to connect to /dev: " << strerror(errno);
        return;
      }
      fbl::unique_fd out;
      zx_status_t status =
          device_watcher::RecursiveWaitForFile(devfs_root.fd(), kTapctlRelativePath, &out);
      if (status != ZX_OK) {
        FX_PLOGS(ERROR, status) << "isolated-devmgr failed while waiting for path "
                                << kTapctlRelativePath;
      }
    });

    zx_status_t status = fidl::WireCall(devfs_root.directory())
                             ->Clone(fuchsia_io::wire::kCloneFlagSameRights,
                                     fidl::ServerEnd<fuchsia_io::Node>(std::move(req)))
                             .status();
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "failed to connect request to /dev";
    }
  });
  net_context.SetNetworkTunHandler(
      [&context](fidl::InterfaceRequest<fuchsia::net::tun::Control> req) {
        zx_status_t status = context->svc()->Connect<fuchsia::net::tun::Control>(std::move(req));
        if (status != ZX_OK) {
          FX_PLOGS(ERROR, status) << "failed to connect request to "
                                  << fuchsia::net::tun::Control::Name_;
        }
      });
  context->outgoing()->AddPublicService(net_context.GetHandler());
  return loop.Run();
}
