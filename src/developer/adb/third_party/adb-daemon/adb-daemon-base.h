// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_ADB_THIRD_PARTY_ADB_DAEMON_ADB_DAEMON_BASE_H_
#define SRC_DEVELOPER_ADB_THIRD_PARTY_ADB_DAEMON_ADB_DAEMON_BASE_H_

#include <lib/zx/socket.h>
#include <lib/zx/status.h>

#include "types.h"

namespace adb_daemon {

class AdbDaemonBase {
 public:
  AdbDaemonBase() = default;
  virtual ~AdbDaemonBase() = default;

  virtual void SendUsbPacket(uint8_t* buf, size_t len, apacket* p, bool release) = 0;
  virtual zx::result<zx::socket> GetServiceSocket(const std::string& service_name,
                                                  std::string args) = 0;
};

}  // namespace adb_daemon

#endif  // SRC_DEVELOPER_ADB_THIRD_PARTY_ADB_DAEMON_ADB_DAEMON_BASE_H_
