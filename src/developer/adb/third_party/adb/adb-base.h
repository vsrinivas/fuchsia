// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_ADB_THIRD_PARTY_ADB_ADB_BASE_H_
#define SRC_DEVELOPER_ADB_THIRD_PARTY_ADB_ADB_BASE_H_

#include <lib/zx/socket.h>
#include <lib/zx/status.h>

#include <string_view>

namespace adb {

class AdbBase {
 public:
  AdbBase() = default;
  virtual ~AdbBase() = default;

  virtual bool SendUsbPacket(uint8_t* buf, size_t len) = 0;
  virtual zx::status<zx::socket> GetServiceSocket(std::string_view service,
                                                  std::string_view args) = 0;
};

}  // namespace adb

#endif  // SRC_DEVELOPER_ADB_THIRD_PARTY_ADB_ADB_BASE_H_
