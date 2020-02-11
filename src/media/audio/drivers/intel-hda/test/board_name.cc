// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <array>

#include <fbl/string.h>
#include <fbl/unique_fd.h>

namespace audio::intel_hda {

constexpr int kMaxBoardNameSize = 128;

// Get the name of the board we are running on.
zx_status_t GetBoardName(fbl::String* result) {
  // Open sysinfo file.
  constexpr char kSysInfoPath[] = "/svc/fuchsia.sysinfo.SysInfo";
  fbl::unique_fd fd(open(kSysInfoPath, O_RDWR));
  if (!fd) {
    return ZX_ERR_INTERNAL;
  }

  // Open service handle.
  zx::channel channel;
  zx_status_t status = fdio_get_service_handle(fd.release(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  // Fetch the board name.
  std::array<char, kMaxBoardNameSize> board_name;
  size_t actual_size;
  zx_status_t fidl_status = fuchsia_sysinfo_SysInfoGetBoardName(
      channel.get(), &status, board_name.data(), board_name.max_size(), &actual_size);
  if (fidl_status != ZX_OK) {
    return fidl_status;
  }
  if (status != ZX_OK) {
    return status;
  }

  *result = fbl::String(board_name.data(), actual_size);
  return ZX_OK;
}

}  // namespace audio::intel_hda
