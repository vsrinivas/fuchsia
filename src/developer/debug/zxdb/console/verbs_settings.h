// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_VERBS_SETTINGS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_VERBS_SETTINGS_H_

#include <string>
#include <vector>

#include "src/developer/debug/zxdb/common/err_or.h"

namespace zxdb {

class ConsoleContext;
class ExecutionScope;

struct ParsedSetCommand {
  enum Operation {
    kAssign,  // =    Sets a complete value for the setting.
    kAppend,  // +=   Appends values to the setting (list only).
    kRemove,  // -=   Removes values from the list (list only).
  };

  std::string name;
  Operation op = kAssign;

  // The things to add/assign/remove.
  std::vector<std::string> values;
};

// Parses the command-line for the "set" command which has its own mini expression grammar.
ErrOr<ParsedSetCommand> ParseSetCommand(const std::string& input);

// Parses an execution scope argument. This is exposed for unit testing.
ErrOr<ExecutionScope> ParseExecutionScope(ConsoleContext* console_context,
                                          const std::string& input);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_VERBS_SETTINGS_H_
