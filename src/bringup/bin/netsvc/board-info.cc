// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "board-info.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "lib/fidl/cpp/message_part.h"
#include "lib/fidl/llcpp/traits.h"
#include "zircon/types.h"
#undef VMOID_INVALID
#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <fuchsia/sysinfo/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/netboot.h>
#include <zircon/device/block.h>
#include <zircon/status.h>

#include <algorithm>
#include <memory>

#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <gpt/gpt.h>

#include "src/lib/bootfs/parser.h"

namespace {

fbl::unique_fd FindGpt() {
  constexpr char kBlockDevPath[] = "/dev/class/block/";
  fbl::unique_fd d_fd(open(kBlockDevPath, O_RDONLY));
  if (!d_fd) {
    fprintf(stderr, "netsvc: Cannot inspect block devices\n");
    return fbl::unique_fd();
  }
  DIR* d = fdopendir(d_fd.release());
  if (d == nullptr) {
    fprintf(stderr, "netsvc: Cannot inspect block devices\n");
    return fbl::unique_fd();
  }
  const auto closer = fbl::MakeAutoCall([&]() { closedir(d); });

  char path[PATH_MAX] = {};

  struct dirent* de;
  while ((de = readdir(d)) != nullptr) {
    fbl::unique_fd fd(openat(dirfd(d), de->d_name, O_RDWR));
    if (!fd) {
      continue;
    }

    zx::channel dev;
    zx_status_t status = fdio_get_service_handle(fd.release(), dev.reset_and_get_address());
    if (status != ZX_OK) {
      continue;
    }

    auto result =
        ::llcpp::fuchsia::hardware::block::Block::Call::GetInfo(zx::unowned_channel(dev.get()));
    if (!result.ok()) {
      continue;
    }
    const auto& info_response = result.value();
    if (info_response.status != ZX_OK) {
      continue;
    }
    auto result2 = ::llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(
        zx::unowned_channel(dev.get()));
    if (result2.status() != ZX_OK) {
      continue;
    }
    const auto& path_response = result2.value();
    if (path_response.result.is_err()) {
      continue;
    }

    auto& r = path_response.result.response();
    path[PATH_MAX - 1] = '\0';
    strncpy(path, r.path.data(), std::min<size_t>(PATH_MAX, r.path.size()));

    // TODO(ZX-1344): This is a hack, but practically, will work for our
    // usage.
    //
    // The GPT which will contain an FVM should be the first non-removable
    // block device that isn't a partition itself.
    if (!(info_response.info->flags & BLOCK_FLAG_REMOVABLE) && strstr(path, "part-") == nullptr) {
      return fbl::unique_fd(open(path, O_RDWR));
    }
  }

  return fbl::unique_fd();
}  // namespace

[[maybe_unused]] static bool IsChromebook(const zx::channel& sysinfo) {
  auto result = ::llcpp::fuchsia::sysinfo::SysInfo::Call::GetBootloaderVendor(zx::unowned(sysinfo));
  zx_status_t status = result.ok() ? result->status : result.status();
  if (status != ZX_OK) {
    return status;
  }
  return strncmp(result->vendor.data(), "coreboot", result->vendor.size()) == 0;
}

zx_status_t GetBoardName(const zx::channel& sysinfo, char* real_board_name) {
  auto result = ::llcpp::fuchsia::sysinfo::SysInfo::Call::GetBoardName(zx::unowned(sysinfo));
  if (!result.ok()) {
    return false;
  }
  const auto& response = result.value();
  if (response.status != ZX_OK) {
    return false;
  }

  size_t strlen = std::min<size_t>(ZX_MAX_NAME_LEN, response.name.size());
  strncpy(real_board_name, response.name.data(), strlen);
  if (strlen == ZX_MAX_NAME_LEN) {
    real_board_name[strlen - 1] = '\0';
  }

  // Special case x64. All x64 boards should get one of "chromebook-x64" or "x64" instead of the
  // more specific name from the BIOS (e.g. "NUC7i5DNHE".)
#if __x86_64__
  if (IsChromebook(sysinfo)) {
    strcpy(real_board_name, "chromebook-x64");
  } else {
    strcpy(real_board_name, "x64");
  }
#endif

  return ZX_OK;
}

zx_status_t GetBoardRevision(const zx::channel& sysinfo, uint32_t* board_revision) {
  auto result = ::llcpp::fuchsia::sysinfo::SysInfo::Call::GetBoardRevision(zx::unowned(sysinfo));
  if (!result.ok()) {
    return false;
  }
  const auto& response = result.value();
  if (response.status != ZX_OK) {
    return false;
  }
  *board_revision = response.revision;
  return ZX_OK;
}

}  // namespace

bool CheckBoardName(const zx::channel& sysinfo, const char* name, size_t length) {
  if (!sysinfo) {
    return false;
  }

  char real_board_name[ZX_MAX_NAME_LEN] = {};
  if (GetBoardName(sysinfo, real_board_name) != ZX_OK) {
    return false;
  }
  length = std::min(length, ZX_MAX_NAME_LEN);

  return strncmp(real_board_name, name, length) == 0;
}

bool ReadBoardInfo(const zx::channel& sysinfo, void* data, off_t offset, size_t* length) {
  if (!sysinfo) {
    return false;
  }

  if (*length < sizeof(board_info_t) || offset != 0) {
    return false;
  }

  auto* board_info = static_cast<board_info_t*>(data);
  memset(board_info, 0, sizeof(*board_info));

  if (GetBoardName(sysinfo, board_info->board_name) != ZX_OK) {
    return false;
  }

  if (GetBoardRevision(sysinfo, &board_info->board_revision) != ZX_OK) {
    return false;
  }

  *length = sizeof(board_info_t);
  return true;
}

size_t BoardInfoSize() { return sizeof(board_info_t); }
