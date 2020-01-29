// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/wait_for_minfs.h"

#include <fcntl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <memory>
#include <utility>

#include <fbl/unique_fd.h>

#include "src/lib/files/file.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/string_view.h"

namespace modular {

// For polling minfs.
constexpr fxl::StringView kPersistentFileSystem = "/data";
constexpr fxl::StringView kMinFsName = "minfs";
constexpr zx::duration kMaxPollingDelay = zx::sec(10);

void WaitForMinfs() {
  auto delay = zx::msec(10);
  zx::time now = zx::clock::get_monotonic();
  while (zx::clock::get_monotonic() - now < kMaxPollingDelay) {
    fbl::unique_fd fd(open(kPersistentFileSystem.data(), O_RDONLY));
    if (fd.is_valid()) {
      fuchsia_io_FilesystemInfo info;
      zx_status_t status, io_status;
      fdio_cpp::FdioCaller caller{std::move(fd)};
      io_status = fuchsia_io_DirectoryAdminQueryFilesystem(caller.borrow_channel(), &status, &info);
      if (io_status == ZX_OK && status == ZX_OK) {
        const char* name = reinterpret_cast<const char*>(info.name);
        fxl::StringView fs_name(name, strnlen(name, fuchsia_io_MAX_FS_NAME_BUFFER));
        if (fs_name == kMinFsName) {
          return;
        }
      }
    }

    usleep(delay.to_usecs());
    delay = delay * 2;
  }

  FXL_LOG(WARNING) << kPersistentFileSystem
                   << " is not persistent. Did you forget to configure it?";
}

}  // namespace modular
