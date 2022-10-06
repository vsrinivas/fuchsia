// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_SEQUENCE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_SEQUENCE_H_

#include <string>
#include <vector>

#include "src/developer/debug/zxdb/common/err_or.h"
#include "src/developer/debug/zxdb/console/command_context.h"

namespace zxdb {

class Console;

// Executes the given list of string commands on the console. It stops until all commands complete
// or there is an error. The callback will be called on either form of completion.
void RunCommandSequence(Console* console, std::vector<std::string> commands,
                        fxl::RefPtr<CommandContext> cmd_context);

// Reads each line from a file and returns it in the given vector. This is used to read script files
// into a sequence of commands.
ErrOr<std::vector<std::string>> ReadCommandsFromFile(const std::string& path);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_SEQUENCE_H_
