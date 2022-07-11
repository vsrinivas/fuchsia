// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <string.h>
#include <zircon/status.h>

#include <fbl/unique_fd.h>

#include "lib/vfs/cpp/vmo_file.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

// This a test component, with the sole job of providing a fake /boot to its parent, who later will
// reroute it to its child.

int main(int argc, char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line, {"early-boot-instrumentation", "boot-provider"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Add prof_data dir.
  auto* boot = context->outgoing()->GetOrCreateDirectory("boot");
  auto kernel = std::make_unique<vfs::PseudoDir>();
  auto data = std::make_unique<vfs::PseudoDir>();
  auto phys = std::make_unique<vfs::PseudoDir>();

  // Fake Kernel vmo.
  zx::vmo kernel_vmo;
  if (auto res = zx::vmo::create(4096, 0, &kernel_vmo); res != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create Kernel VMO. Error: " << zx_status_get_string(res);
  } else {
    if (auto res = kernel_vmo.write("kernel", 0, 7); res != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to write Kernel VMO contents. Error: " << zx_status_get_string(res);
    }
    auto kernel_file = std::make_unique<vfs::VmoFile>(std::move(kernel_vmo), 4096);
    data->AddEntry("zircon.elf.profraw", std::move(kernel_file));
  }

  // Fake Physboot VMO.
  zx::vmo phys_vmo;
  if (auto res = zx::vmo::create(4096, 0, &phys_vmo); res != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create Physboot VMO. Error: " << zx_status_get_string(res);
  } else {
    if (auto res = phys_vmo.write("physboot", 0, 9); res != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to write Kernel VMO contents. Error: " << zx_status_get_string(res);
    }
    auto physboot_file = std::make_unique<vfs::VmoFile>(std::move(phys_vmo), 4096);
    phys->AddEntry("physboot.profraw", std::move(physboot_file));
  }

  // outgoing/boot/kernel/data/phys
  data->AddEntry("phys", std::move(phys));
  // outgoing/boot/kernel/data
  kernel->AddEntry("data", std::move(data));
  // outgoing/boot/kernel
  boot->AddEntry("kernel", std::move(kernel));

  loop.Run();
  return 0;
}
