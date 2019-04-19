// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_CRASHPAD_AGENT_CONFIG_H_
#define SRC_DEVELOPER_CRASHPAD_AGENT_CONFIG_H_

#include <stdint.h>
#include <zircon/types.h>

#include <memory>
#include <string>

namespace fuchsia {
namespace crash {

// Crash analysis configuration.
struct Config {
  // Directory path under which to store the local Crashpad database.
  std::string local_crashpad_database_path;

  // Maximum size (in kilobytes) that the local Crashpad database should grow
  // to, excluding current reports being generated.
  uint64_t max_crashpad_database_size_in_kb;

  // Whether to upload the crash report to a remote crash server or leave it
  // locally.
  bool enable_upload_to_crash_server = false;

  // URL of the remote crash server.
  // We use a std::unique_ptr to set it only when relevant, i.e. when
  // |enable_upload_to_crash_server| is set.
  std::unique_ptr<std::string> crash_server_url;
};

// Parses the JSON config at |filepath| as |config|.
zx_status_t ParseConfig(const std::string& filepath, Config* config);

}  // namespace crash
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_CRASHPAD_AGENT_CONFIG_H_
