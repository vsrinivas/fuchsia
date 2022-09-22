// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_run_test.h"

#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {
namespace {

const char kShortHelp[] = "run-test: Run the test.";
const char kHelp[] =
    R"(run-test <url> [ <case filter>* ]

  Runs the test with the given URL. Optional case filters can be provided to
  specify the test cases to run. The test will be launched in a similar fashion
  as "ffx test run" on host or "run-test-suite" on Fuchsia.

  Since Fuchsia test runners usually start one process for each test case,
  running one test could spawns many processes in the debugger. The process name
  of these processes will be overridden as the test case name, making it easier
  to navigate between test cases.

Arguments

  <url>
      The URL of the test to run.

  <case filter>*
      Glob patterns for matching tests. Can be specified multiple times to pass
      in multiple patterns. Tests may be excluded by prepending a '-' to the
      glob pattern.

Examples

  run-test fuchsia-pkg://fuchsia.com/pkg#meta/some_test.cm SomeTest.Case1
)";

void Exec(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  // No nouns should be provided.
  if (Err err = cmd.ValidateNouns({}); err.has_error()) {
    return cmd_context->ReportError(err);
  }

  if (cmd.args().empty()) {
    return cmd_context->ReportError(Err("No test to run. Try \"run-test <url>\"."));
  }

  if (cmd.args()[0].find("://") == std::string::npos || !StringEndsWith(cmd.args()[0], ".cm")) {
    return cmd_context->ReportError(
        Err("The first argument must be a component URL. Try \"help run-test\"."));
  }

  // Launch the test.
  debug_ipc::LaunchRequest request;
  request.inferior_type = debug_ipc::InferiorType::kTest;
  request.argv = cmd.args();

  cmd.target()->session()->remote_api()->Launch(
      request, [cmd_context](Err err, debug_ipc::LaunchReply reply) mutable {
        if (!err.has_error() && reply.status.has_error()) {
          err = Err("Could not start test: %s", reply.status.message().c_str());
        }
        if (err.has_error()) {
          cmd_context->ReportError(err);
        }
      });
}

}  // namespace

VerbRecord GetRunTestVerbRecord() {
  return {&Exec, {"run-test"}, kShortHelp, kHelp, CommandGroup::kProcess};
}

}  // namespace zxdb
