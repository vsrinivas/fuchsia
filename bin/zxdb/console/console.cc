// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/console.h"

#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>

#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_parser.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

void OnTargetStateChange(Target* target, Target::State old_state) {
  if (target->state() == Target::State::kStopped &&
      old_state == Target::State::kRunning) {
    // Only print info for running->stopped transitions. The launch
    // callback will be called for succeeded and failed starts
    // (starting->stopped and starting->running) with the appropriate error
    // or information.
    Console::get()->Output(fxl::StringPrintf(
        "Process %zu exited.", target->target_id()));
  }

}

void OnThreadChange(Thread* thread, Process::ThreadChange change) {
  OutputBuffer out;
  out.Append(fxl::StringPrintf("Process %zu thread %zu ",
      thread->process()->target()->target_id(), thread->thread_id()));
  if (change == Process::ThreadChange::kStarted) {
    out.Append(fxl::StringPrintf("started with koid %" PRIu64 ".",
                                 thread->koid()));
  } else {
    out.Append("exited.");
  }
  Console::get()->Output(out);
}

}  // namespace

Console* Console::singleton_ = nullptr;

Console::Console(Session* session)
    : session_(session), line_input_("[zxdb] ") {
  FXL_DCHECK(!singleton_);
  singleton_ = this;

  line_input_.set_completion_callback(&GetCommandCompletions);

  // Set stdin to async mode or OnStdinReadable will block.
  fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);

  // Register for callbacks to notify the user of important changes.
  target_state_change_callback_id_ = Target::StartWatchingGlobalStateChanges(
      &OnTargetStateChange);
  thread_change_callback_id_ = Process::StartWatchingGlobalThreadChanges(
      &OnThreadChange);
}

Console::~Console() {
  Process::StopWatchingGlobalThreadChanges(thread_change_callback_id_);
  Target::StopWatchingGlobalStateChanges(target_state_change_callback_id_);

  FXL_DCHECK(singleton_ == this);
  singleton_ = nullptr;
}

void Console::Init() {
  line_input_.BeginReadLine();
}

Console::Result Console::OnInput(char c) {
  if (line_input_.OnInput(c)) {
    Result result = DispatchInputLine(line_input_.line());
    if (result == Result::kQuit)
      return result;
    line_input_.BeginReadLine();
  }
  return Result::kContinue;
}

void Console::Output(OutputBuffer output) {
  // Since most operations are asynchronous, we have to hide the input line
  // before printing anything or it will get appended to whatever the user is
  // typing on the screen.
  //
  // TODO(brettw) This can cause flickering. A more advanced system would
  // do more fancy console stuff to output above the input line so we'd
  // never have to hide it.
  line_input_.Hide();
  output.WriteToStdout();
  line_input_.Show();
}

void Console::Output(const std::string& s) {
  OutputBuffer buffer;
  buffer.Append(s);
  Output(std::move(buffer));
}

Console::Result Console::DispatchInputLine(const std::string& line) {
  Command cmd;
  Err err = ParseCommand(line, &cmd);

  if (err.ok()) {
    if (cmd.noun == Noun::kZxdb && cmd.verb == Verb::kQuit)
      return Result::kQuit;
    else if (cmd.noun != Noun::kNone)
      err = DispatchCommand(session_, cmd);
  }

  if (err.has_error()) {
    OutputBuffer out;
    out.OutputErr(err);
    Output(std::move(out));
  }
  return Result::kContinue;
}

}  // namespace zxdb
