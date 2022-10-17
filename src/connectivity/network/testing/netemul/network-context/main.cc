// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/network/testing/netemul/network-context/lib/network_context.h"
#include "src/connectivity/network/testing/netemul/network-context/lib/realm_setup.h"

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
      zx::result result = netemul::StartDriverTestRealm();
      if (result.is_error()) {
        FX_PLOGS(ERROR, result.status_value()) << "failed while starting driver test realm";
        return;
      }
      devfs_root.reset(std::move(result.value()));
    });

    zx_status_t status = fidl::WireCall(devfs_root.directory())
                             ->Clone(fuchsia_io::wire::OpenFlags::kCloneSameRights,
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
