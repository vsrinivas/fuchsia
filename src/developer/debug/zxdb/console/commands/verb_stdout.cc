// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_stdout.h"

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

const char kStdoutShortHelp[] = "stdout: Show process output.";

template <typename ContainerType>
std::string OutputContainer(const ContainerType& container) {
  std::string str;
  str.resize(container.size());
  str.insert(str.end(), container.begin(), container.end());
  return str;
}

void RunVerbStdout(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  return RunVerbStdio(Verb::kStdout, cmd, cmd_context);
}

}  // namespace

VerbRecord GetStdoutVerbRecord() {
  return VerbRecord(&RunVerbStdout, {"stdout"}, kStdoutShortHelp, kStdioHelp,
                    CommandGroup::kProcess);
}

const char kStdioHelp[] =
    R"(stdout | stderr

  Shows the stdout/stderr (depending on the command) for a given process.

  zxdb will store the output of a debugged process in a ring buffer in order to
  have it available after the fact. This is independent on whether the output
  is being silenced by the "show-stdout" setting (Run "get" to see the current
  settings, run "help get" and "help set" for more information on settings).

Examples

  // Shows stdout of the current active process.
  stdout
    This is some stdout output.
    This is another stdout output.

  // Shows stderr of process 2.
  pr 2 stderr
    [ERROR] This is a stderr entry.
)";

void RunVerbStdio(Verb io_type, const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  FX_DCHECK(io_type == Verb::kStdout || io_type == Verb::kStderr);

  // Only a process can be specified.
  if (Err err = cmd.ValidateNouns({Noun::kProcess}); err.has_error())
    return cmd_context->ReportError(err);

  const char* io_name = io_type == Verb::kStdout ? "stdout" : "stderr";
  if (!cmd.args().empty()) {
    auto msg = fxl::StringPrintf("\"%s\" takes no parameters.", io_name);
    return cmd_context->ReportError(Err(ErrType::kInput, std::move(msg)));
  }

  if (Err err = AssertRunningTarget(cmd_context->GetConsoleContext(), io_name, cmd.target());
      err.has_error())
    return cmd_context->ReportError(err);

  Process* process = cmd.target()->GetProcess();
  auto& container = io_type == Verb::kStdout ? process->get_stdout() : process->get_stderr();
  cmd_context->Output(OutputContainer(container));
}

}  // namespace zxdb
