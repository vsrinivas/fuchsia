// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_until.h"

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/until_thread_controller.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/input_location_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kUntilShortHelp[] = "until / u: Runs a thread until a location is reached.";
const char kUntilHelp[] =
    R"(until <location>

  Alias: "u"

  Continues execution of a thread or a process until a given location is
  reached. You could think of this command as setting an implicit one-shot
  breakpoint at the given location and continuing execution.

  Normally this operation will apply only to the current thread. To apply to
  all threads in a process, use "process until" (see the examples below).

  See also "finish".

Location arguments

  Current frame's address (no input)
    until

)" LOCATION_ARG_HELP("until")
        R"(
Examples

  u
  until
      Runs until the current frame's location is hit again. This can be useful
      if the current code is called in a loop to advance to the next iteration
      of the current code.

  f 1 u
  frame 1 until
      Runs until the given frame's location is hit. Since frame 1 is
      always the current function's calling frame, this command will normally
      stop when the current function returns. The exception is if the code
      in the calling function is called recursively from the current location,
      in which case the next invocation will stop ("until" does not match
      stack frames on break). See "finish" for a stack-aware version.

  u 24
  until 24
      Runs the current thread until line 24 of the current frame's file.

  until foo.cc:24
      Runs the current thread until the given file/line is reached.

  thread 2 until 24
  process 1 thread 2 until 24
      Runs the specified thread until line 24 is reached. When no filename is
      given, the specified thread's currently selected frame will be used.

  u MyClass::MyFunc
  until MyClass::MyFunc
      Runs the current thread until the given function is called.

  pr u MyClass::MyFunc
  process until MyClass::MyFunc
      Continues all threads of the current process, stopping the next time any
      of them call the function.
)";

void RunVerbUntil(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  // Decode the location.
  //
  // The validation on this is a bit tricky. Most uses apply to the current thread and take some
  // implicit information from the current frame (which requires the thread be stopped). But when
  // doing a process-wide one, don't require a currently stopped thread unless it's required to
  // compute the location.
  std::vector<InputLocation> locations;
  if (cmd.args().empty()) {
    // No args means use the current location.
    if (!cmd.frame()) {
      return cmd_context->ReportError(
          Err(ErrType::kInput, "There isn't a current frame to take the location from."));
    }
    locations.emplace_back(cmd.frame()->GetAddress());
  } else if (cmd.args().size() == 1) {
    // One arg = normal location (this function can handle null frames).
    if (Err err = ParseLocalInputLocation(cmd.frame(), cmd.args()[0], &locations); err.has_error())
      return cmd_context->ReportError(err);
  } else {
    return cmd_context->ReportError(
        Err(ErrType::kInput,
            "Expecting zero or one arg for the location.\n"
            "Formats: <function>, <file>:<line#>, <line#>, or 0x<address>"));
  }

  auto callback = [cmd_context](const Err& err) {
    if (err.has_error())
      cmd_context->ReportError(err);
  };

  ConsoleContext* console_context = cmd_context->GetConsoleContext();

  // Dispatch the request.
  if (cmd.HasNoun(Noun::kProcess) && !cmd.HasNoun(Noun::kThread) && !cmd.HasNoun(Noun::kFrame)) {
    // Process-wide ("process until ...").
    if (Err err = AssertRunningTarget(console_context, "until", cmd.target()); err.has_error())
      return cmd_context->ReportError(err);
    cmd.target()->GetProcess()->ContinueUntil(locations, callback);
  } else {
    // Thread-specific.
    if (Err err = AssertStoppedThreadWithFrameCommand(console_context, cmd, "until");
        err.has_error())
      return cmd_context->ReportError(err);

    auto controller = std::make_unique<UntilThreadController>(
        std::move(locations), fit::defer_callback([cmd_context]() {}));
    cmd.thread()->ContinueWith(std::move(controller), [cmd_context](const Err& err) {
      if (err.has_error())
        cmd_context->ReportError(err);
    });
  }
}

}  // namespace

VerbRecord GetUntilVerbRecord() {
  return VerbRecord(&RunVerbUntil, &CompleteInputLocation, {"until", "u"}, kUntilShortHelp,
                    kUntilHelp, CommandGroup::kStep);
}

}  // namespace zxdb
