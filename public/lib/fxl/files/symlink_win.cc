// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/files/symlink.h"

#include <windows.h>
#include <winioctl.h>

#include <iostream>

#include "lib/fxl/logging.h"
#include "lib/fxl/memory/unique_object.h"

namespace files {

struct UniqueHandleTraits {
  static HANDLE InvalidValue() { return INVALID_HANDLE_VALUE; }
  static bool IsValid(HANDLE value) { return value != INVALID_HANDLE_VALUE; }
  static void Free(HANDLE value) { CloseHandle(value); }
};

bool ReadSymbolicLink(const std::string& path, std::string* resolved_path) {
  FXL_CHECK(false) << "Unimplemented";
  return false;
}

std::string GetAbsoluteFilePath(const std::string& path) {
  fxl::UniqueObject<HANDLE, UniqueHandleTraits> file(
      CreateFileA(path.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                  OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL));
  if (!file.is_valid()) {
    return std::string();
  }
  char buffer[MAX_PATH];
  DWORD ret = GetFinalPathNameByHandleA(file.get(), buffer, MAX_PATH,
                                        FILE_NAME_NORMALIZED);
  if (ret == 0 || ret > MAX_PATH) {
    return std::string();
  }
  std::string result(buffer);
  result.erase(0, strlen("\\\\?\\"));
  return result;
}

}  // namespace files
