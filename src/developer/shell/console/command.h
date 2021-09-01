// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_CONSOLE_COMMAND_H_
#define SRC_DEVELOPER_SHELL_CONSOLE_COMMAND_H_

#include <string>
#include <vector>

#include "fidl/fuchsia.shell/cpp/wire.h"
#include "src/developer/shell/common/ast_builder.h"
#include "src/developer/shell/common/err.h"

namespace shell::console {

// Represents a command for the shell.
class Command {
 public:
  Command();
  ~Command();

  Command(const Command&) = delete;
  Command& operator=(const Command&) = delete;

  Command(Command&&) = delete;
  Command& operator=(Command&&) = delete;

  bool Parse(const std::string& line);

  const std::string& parse_error() { return parse_error_; }

  AstBuilder& nodes() { return accumulated_nodes_; }

 public:
  std::string parse_error_;
  AstBuilder accumulated_nodes_;
};

}  // namespace shell::console

#endif  // SRC_DEVELOPER_SHELL_CONSOLE_COMMAND_H_
