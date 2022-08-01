// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.boot/cpp/markers.h>
#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/wire/client.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <string.h>
#include <zircon/status.h>

#include <fbl/unique_fd.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/sys/early_boot_instrumentation/coverage_source.h"

int main(int argc, char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line, {"early-boot-instrumentation"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Add prof_data dir.
  auto* prof_data_dir = context->outgoing()->GetOrCreateDirectory("prof-data");
  auto static_prof = std::make_unique<vfs::PseudoDir>();
  auto dynamic_prof = std::make_unique<vfs::PseudoDir>();

  // Even if we fail to populate from the sources, we expose empty directories,
  // such that the contract remains.
  fbl::unique_fd kernel_data_dir(open("/boot/kernel/data", O_RDONLY));
  if (!kernel_data_dir) {
    const char* err = strerror(errno);
    FX_LOGS(ERROR) << "Could not obtain handle to '/boot/kernel/data'. " << err;
  }

  if (auto res =
          early_boot_instrumentation::ExposeKernelProfileData(kernel_data_dir, *dynamic_prof);
      res.is_error()) {
    FX_LOGS(ERROR) << "Could not expose kernel profile data. " << res.status_value();
  }

  fbl::unique_fd phys_data_dir(open("/boot/kernel/data/phys", O_RDONLY));
  if (!phys_data_dir) {
    const char* err = strerror(errno);
    FX_LOGS(ERROR) << "Could not obtain handle to '/boot/kernel/data/phys'. " << err;
  }

  if (auto res = early_boot_instrumentation::ExposePhysbootProfileData(phys_data_dir, *static_prof);
      res.is_error()) {
    FX_LOGS(ERROR) << "Could not expose physboot profile data. " << res.status_value();
  }

  // Get the SvcStash server end.
  zx::channel provider_client, provider_server;
  if (auto res = zx::channel::create(0, &provider_client, &provider_server); res != ZX_OK) {
    FX_LOGS(ERROR) << "Could not create channel for fuchsia.boot.SvcStashProvider. "
                   << zx_status_get_string(res);
  } else if (auto res = fdio_service_connect(
                 fidl::DiscoverableProtocolDefaultPath<fuchsia_boot::SvcStashProvider>,
                 provider_server.release());
             res != ZX_OK) {
    FX_LOGS(ERROR) << "Could not obtain handle to fuchsia.boot.SvcStashProvider. "
                   << zx_status_get_string(res);
  } else {  // Successfully connected to the service.
    fidl::WireSyncClient<fuchsia_boot::SvcStashProvider> provider_fidl_client;
    fidl::ClientEnd<fuchsia_boot::SvcStashProvider> provider_client_end(std::move(provider_client));
    provider_fidl_client.Bind(std::move(provider_client_end));

    auto get_response = provider_fidl_client->Get();

    if (get_response->is_ok()) {
      auto& stash_svc = get_response->value()->resource;
      early_boot_instrumentation::ExposeEarlyBootStashedProfileData(stash_svc.channel().borrow(),
                                                                    *dynamic_prof, *static_prof);
    }
  }

  // outgoing/prof-data/static
  prof_data_dir->AddEntry("static", std::move(static_prof));
  // outgoing/prof-data/dynamic
  prof_data_dir->AddEntry("dynamic", std::move(dynamic_prof));
  loop.Run();
  return 0;
}
