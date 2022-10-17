// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/board-info.h"

#include <dirent.h>
#include <lib/netboot/netboot.h>
#include <string.h>
#include <zircon/types.h>

#include <algorithm>

namespace {

[[maybe_unused]] zx::result<bool> IsChromebook(
    fidl::UnownedClientEnd<fuchsia_sysinfo::SysInfo> sysinfo) {
  fidl::WireResult result = fidl::WireCall(sysinfo)->GetBootloaderVendor();
  zx_status_t status = result.ok() ? result->status : result.status();
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(strncmp(result->vendor.data(), "coreboot", result->vendor.size()) == 0);
}

zx::result<> GetBoardName(fidl::UnownedClientEnd<fuchsia_sysinfo::SysInfo> sysinfo,
                          char* real_board_name) {
  fidl::WireResult result = fidl::WireCall(sysinfo)->GetBoardName();
  if (!result.ok()) {
    return zx::error(result.status());
  }
  const auto& response = result.value();
  if (response.status != ZX_OK) {
    return zx::error(response.status);
  }

  size_t strlen = std::min<size_t>(ZX_MAX_NAME_LEN, response.name.size());
  strncpy(real_board_name, response.name.data(), strlen);
  if (strlen == ZX_MAX_NAME_LEN) {
    real_board_name[strlen - 1] = '\0';
  }

  // Special case x64. All x64 boards should get one of "chromebook-x64" or "x64" instead of the
  // more specific name from the BIOS (e.g. "NUC7i5DNHE".)
#if __x86_64__
  {
    zx::result is_chromebook = IsChromebook(sysinfo);
    if (is_chromebook.is_error()) {
      return is_chromebook.take_error();
    }
    if (is_chromebook.value()) {
      strcpy(real_board_name, "chromebook-x64");
    } else {
      strcpy(real_board_name, "x64");
    }
  }
#endif

  return zx::ok();
}

zx::result<uint32_t> GetBoardRevision(fidl::UnownedClientEnd<fuchsia_sysinfo::SysInfo> sysinfo) {
  fidl::WireResult result = fidl::WireCall(sysinfo)->GetBoardRevision();
  if (!result.ok()) {
    return zx::error(result.status());
  }
  const auto& response = result.value();
  if (response.status != ZX_OK) {
    return zx::error(response.status);
  }
  return zx::ok(response.revision);
}

}  // namespace

zx::result<bool> CheckBoardName(fidl::UnownedClientEnd<fuchsia_sysinfo::SysInfo> sysinfo,
                                const char* name, size_t length) {
  if (!sysinfo) {
    return zx::error(ZX_ERR_BAD_HANDLE);
  }

  char real_board_name[ZX_MAX_NAME_LEN] = {};

  if (zx::result<> status = GetBoardName(sysinfo, real_board_name); status.is_error()) {
    return status.take_error();
  }
  length = std::min(length, ZX_MAX_NAME_LEN);

  return zx::ok(strncmp(real_board_name, name, length) == 0);
}

zx::result<> ReadBoardInfo(fidl::UnownedClientEnd<fuchsia_sysinfo::SysInfo> sysinfo, void* data,
                           off_t offset, size_t* length) {
  if (!sysinfo) {
    return zx::error(ZX_ERR_BAD_HANDLE);
  }

  if (*length < sizeof(board_info_t) || offset != 0) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  auto* board_info = static_cast<board_info_t*>(data);
  memset(board_info, 0, sizeof(*board_info));

  if (zx::result<> status = GetBoardName(sysinfo, board_info->board_name); status.is_error()) {
    return status.take_error();
  }

  {
    zx::result status = GetBoardRevision(sysinfo);
    if (status.is_error()) {
      return status.take_error();
    }
    // NB: memcpy here to avoid possible misaligned access since we receive raw
    // data bytes from caller.
    static_assert(sizeof(board_info->board_revision) == sizeof(status.value()));
    memcpy(&board_info->board_revision, &status.value(), sizeof(board_info->board_revision));
  }

  *length = sizeof(board_info_t);
  return zx::ok();
}

size_t BoardInfoSize() { return sizeof(board_info_t); }
