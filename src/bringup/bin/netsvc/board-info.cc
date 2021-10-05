// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "board-info.h"

#include <dirent.h>
#include <string.h>
#include <zircon/boot/netboot.h>
#include <zircon/types.h>

#include <algorithm>

namespace {

[[maybe_unused]] static bool IsChromebook(
    fidl::UnownedClientEnd<fuchsia_sysinfo::SysInfo> sysinfo) {
  fidl::WireResult result = fidl::WireCall(sysinfo)->GetBootloaderVendor();
  zx_status_t status = result.ok() ? result->status : result.status();
  if (status != ZX_OK) {
    return status;
  }
  return strncmp(result->vendor.data(), "coreboot", result->vendor.size()) == 0;
}

zx_status_t GetBoardName(fidl::UnownedClientEnd<fuchsia_sysinfo::SysInfo> sysinfo,
                         char* real_board_name) {
  fidl::WireResult result = fidl::WireCall(sysinfo)->GetBoardName();
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

zx_status_t GetBoardRevision(fidl::UnownedClientEnd<fuchsia_sysinfo::SysInfo> sysinfo,
                             uint32_t* board_revision) {
  fidl::WireResult result = fidl::WireCall(sysinfo)->GetBoardRevision();
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

bool CheckBoardName(fidl::UnownedClientEnd<fuchsia_sysinfo::SysInfo> sysinfo, const char* name,
                    size_t length) {
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

bool ReadBoardInfo(fidl::UnownedClientEnd<fuchsia_sysinfo::SysInfo> sysinfo, void* data,
                   off_t offset, size_t* length) {
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
