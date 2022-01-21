// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/early_boot_instrumentation/coverage_source.h"

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <lib/zx/status.h>
#include <unistd.h>

#include <string>
#include <string_view>

#include <fbl/unique_fd.h>
#include <sdk/lib/vfs/cpp/pseudo_dir.h>
#include <sdk/lib/vfs/cpp/vmo_file.h>

namespace early_boot_instrumentation {
namespace {

constexpr std::string_view kKernelProfRaw = "zircon.elf.profraw";
constexpr std::string_view kKernelSymbolizerLog = "symbolizer.log";

struct ExportedFd {
  fbl::unique_fd fd;
  std::string export_name;
};

}  // namespace

zx::status<> ExposeKernelProfileData(fbl::unique_fd& kernel_data_dir, vfs::PseudoDir& out_dir) {
  std::vector<ExportedFd> exported_fds;

  fbl::unique_fd kernel_profile(openat(kernel_data_dir.get(), kKernelProfRaw.data(), O_RDONLY));
  if (!kernel_profile) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  exported_fds.emplace_back(
      ExportedFd{.fd = std::move(kernel_profile), .export_name = std::string(kKernelFile)});

  fbl::unique_fd kernel_log(openat(kernel_data_dir.get(), kKernelSymbolizerLog.data(), O_RDONLY));
  if (kernel_log) {
    exported_fds.emplace_back(
        ExportedFd{.fd = std::move(kernel_log), .export_name = std::string(kKernelSymbolizerFile)});
  }

  for (auto& [fd, export_as] : exported_fds) {
    // Get the underlying vmo of the fd.
    zx::vmo vmo;
    if (auto res = fdio_get_vmo_clone(fd.get(), vmo.reset_and_get_address()); res != ZX_OK) {
      return zx::error(res);
    }
    size_t size = 0;
    if (auto res = vmo.get_size(&size); res != ZX_OK) {
      return zx::error(res);
    }

    auto file = std::make_unique<vfs::VmoFile>(std::move(vmo), 0, size);
    if (auto res = out_dir.AddEntry(export_as, std::move(file)); res != ZX_OK) {
      return zx::error(res);
    }
  }

  return zx::success();
}

}  // namespace early_boot_instrumentation
