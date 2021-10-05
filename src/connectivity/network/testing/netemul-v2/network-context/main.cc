// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/netemul/devmgr/cpp/fidl.h>
#include <fuchsia/netemul/network/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

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
    // Wait synchronously just once for "/dev/sys/test/tapctl" to be available to ensure the driver
    // is initialized.
    static std::once_flag flag;
    std::call_once(flag, [&devfs_root]() {
      devfs_root.reset(fbl::unique_fd(open("/dev", O_RDONLY)));
      if (!devfs_root) {
        FX_LOGS(ERROR) << "failed to connect to /dev: " << strerror(errno);
      }
      fbl::unique_fd out;
      zx_status_t status =
          devmgr_integration_test::RecursiveWaitForFile(devfs_root.fd(), kTapctlRelativePath, &out);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "isolated-devmgr failed while waiting for path " << kTapctlRelativePath
                       << ": " << zx_status_get_string(status);
      }
    });

    zx_status_t status = fidl::WireCall(devfs_root.directory())
                             ->Clone(fuchsia_io::wire::kCloneFlagSameRights,
                                     fidl::ServerEnd<fuchsia_io::Node>(std::move(req)))
                             .status();
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "failed to connect request to /dev: " << zx_status_get_string(status);
    }
  });
  net_context.SetNetworkTunHandler(
      [&context](fidl::InterfaceRequest<fuchsia::net::tun::Control> req) {
        zx_status_t status = context->svc()->Connect<fuchsia::net::tun::Control>(std::move(req));
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "failed to connect request to " << fuchsia::net::tun::Control::Name_
                         << ": " << zx_status_get_string(status);
        }
      });
  context->outgoing()->AddPublicService(net_context.GetHandler());
  return loop.Run();
}
