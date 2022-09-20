// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_disconnect.h"

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kDisconnectShortHelp[] = R"(disconnect: Disconnect from the remote system.)";
const char kDisconnectHelp[] =
    R"(disconnect

  Disconnects from the remote system, or cancels an in-progress connection if
  there is one.

  There are no arguments.
)";

void RunVerbDisconnect(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  if (!cmd.args().empty())
    return cmd_context->ReportError(Err(ErrType::kInput, "\"disconnect\" takes no arguments."));

  if (Err err = cmd_context->GetConsoleContext()->session()->Disconnect(); err.has_error())
    return cmd_context->ReportError(err);

  cmd_context->Output("Disconnected successfully.");
}

}  // namespace

VerbRecord GetDisconnectVerbRecord() {
  return VerbRecord(&RunVerbDisconnect, {"disconnect"}, kDisconnectShortHelp, kDisconnectHelp,
                    CommandGroup::kGeneral);
}

}  // namespace zxdb
