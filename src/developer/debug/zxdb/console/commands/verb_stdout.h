// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_STDOUT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_STDOUT_H_

#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

class Command;
class ConsoleContext;
class Err;

VerbRecord GetStdoutVerbRecord();

// Backend shared between the stdout and stderr verbs.
extern const char kStdioHelp[];
void RunVerbStdio(Verb io_type, const Command& cmd, fxl::RefPtr<CommandContext> cmd_context);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_STDOUT_H_
