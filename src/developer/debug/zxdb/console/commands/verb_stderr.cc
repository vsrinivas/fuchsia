// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_stderr.h"

#include "src/developer/debug/zxdb/console/commands/verb_stdout.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kStderrShortHelp[] = "stderr: Show process error output.";

void RunVerbStderr(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  return RunVerbStdio(Verb::kStderr, cmd, cmd_context);
}

}  // namespace

VerbRecord GetStderrVerbRecord() {
  return VerbRecord(&RunVerbStderr, {"stderr"}, kStderrShortHelp, kStdioHelp,
                    CommandGroup::kProcess);
}

}  // namespace zxdb
