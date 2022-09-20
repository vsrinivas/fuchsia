// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/commands/verb_help.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/nouns.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kClsShortHelp[] = "cls: clear screen.";
const char kClsHelp[] =
    R"(cls

  Clears the contents of the console. Similar to "clear" on a shell.

  There are no arguments.
)";

void RunVerbCls(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  if (!cmd.args().empty())
    return cmd_context->ReportError(Err(ErrType::kInput, "\"cls\" takes no arguments."));
  Console::get()->Clear();
}

}  // namespace

VerbRecord GetClsVerbRecord() {
  return VerbRecord(&RunVerbCls, {"cls"}, kClsShortHelp, kClsHelp, CommandGroup::kGeneral);
}

}  // namespace zxdb
