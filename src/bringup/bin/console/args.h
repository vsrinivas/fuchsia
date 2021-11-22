// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_CONSOLE_ARGS_H_
#define SRC_BRINGUP_BIN_CONSOLE_ARGS_H_

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <zircon/status.h>

#include <string>
#include <vector>

struct Options {
  std::vector<std::string> allowed_log_tags;
  std::vector<std::string> denied_log_tags;
};

zx_status_t ParseArgs(int argc, const char** argv,
                      const fidl::WireSyncClient<fuchsia_boot::Arguments>& client, Options* opts);

#endif  // SRC_BRINGUP_BIN_CONSOLE_ARGS_H_
