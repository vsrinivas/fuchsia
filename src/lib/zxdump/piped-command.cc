// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "piped-command.h"

namespace zxdump {

fit::result<std::string> PipedCommand::Start(const std::string& command,
                                             const std::vector<std::string>& args) {
  std::vector<const char*> argv{command.c_str()};
  for (const std::string& arg : args) {
    argv.push_back(arg.c_str());
  }
  argv.push_back(nullptr);

  return StartArgv(argv);
}

}  // namespace zxdump
