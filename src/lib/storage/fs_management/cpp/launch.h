// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_LAUNCH_H_
#define SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_LAUNCH_H_

#include <lib/zx/handle.h>
#include <zircon/types.h>

#include <string>
#include <utility>
#include <vector>

namespace fs_management {

struct LaunchOptions {
  bool sync = true;

  enum class Logging {
    kSilent = 0,
    kStdio = 1,
    kSyslog = 2,
  } logging = Logging::kSyslog;
};

// Callback that will launch the requested program.
using LaunchCallback = zx_status_t (*)(const std::vector<std::string>& args,
                                       std::vector<std::pair<uint32_t, zx::handle>> handles);

zx_status_t Launch(const std::vector<std::string>& args,
                   std::vector<std::pair<uint32_t, zx::handle>> handles,
                   const LaunchOptions& options);

// Creates no logs, waits for process to terminate.
zx_status_t LaunchSilentSync(const std::vector<std::string>& args,
                             std::vector<std::pair<uint32_t, zx::handle>> handles);

// Creates no logs, does not wait for process to terminate.
zx_status_t LaunchSilentAsync(const std::vector<std::string>& args,
                              std::vector<std::pair<uint32_t, zx::handle>> handles);

// Creates stdio logs, waits for process to terminate.
zx_status_t LaunchStdioSync(const std::vector<std::string>& args,
                            std::vector<std::pair<uint32_t, zx::handle>> handles);

// Creates stdio logs, does not wait for process to terminate.
zx_status_t LaunchStdioAsync(const std::vector<std::string>& args,
                             std::vector<std::pair<uint32_t, zx::handle>> handles);

// Creates kernel logs, does not wait for process to terminate.
zx_status_t LaunchLogsAsync(const std::vector<std::string>& args,
                            std::vector<std::pair<uint32_t, zx::handle>> handles);

}  // namespace fs_management

#endif  // SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_LAUNCH_H_
