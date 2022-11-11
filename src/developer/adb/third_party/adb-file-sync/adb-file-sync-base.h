// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_ADB_THIRD_PARTY_ADB_FILE_SYNC_ADB_FILE_SYNC_BASE_H_
#define SRC_DEVELOPER_ADB_THIRD_PARTY_ADB_FILE_SYNC_ADB_FILE_SYNC_BASE_H_

#include <lib/zx/result.h>
#include <lib/zx/socket.h>

#include <string>

namespace adb_file_sync {

class AdbFileSyncBase {
 public:
  AdbFileSyncBase() = default;
  virtual ~AdbFileSyncBase() = default;

  virtual zx::result<zx::channel> ConnectToComponent(std::string name,
                                                     std::vector<std::string>* out_path) = 0;
};

}  // namespace adb_file_sync

#endif  // SRC_DEVELOPER_ADB_THIRD_PARTY_ADB_FILE_SYNC_ADB_FILE_SYNC_BASE_H_
