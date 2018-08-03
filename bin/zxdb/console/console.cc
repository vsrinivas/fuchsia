// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/console.h"

#include <fcntl.h>
#include <unistd.h>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_parser.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

Console* Console::singleton_ = nullptr;

Console::Console(Session* session) : context_(session), line_input_("[zxdb] ") {
  FXL_DCHECK(!singleton_);
  singleton_ = this;

  line_input_.set_completion_callback(&GetCommandCompletions);

  // Set stdin to async mode or OnStdinReadable will block.
  fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
}

Console::~Console() {
  FXL_DCHECK(singleton_ == this);
  singleton_ = nullptr;
}

void Console::Init() {
  line_input_.BeginReadLine();

  stdio_watch_ = debug_ipc::MessageLoop::Current()->WatchFD(
      debug_ipc::MessageLoop::WatchMode::kRead, STDIN_FILENO, this);
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

void Console::Output(const Err& err) {
  OutputBuffer buffer;
  buffer.OutputErr(err);
  Output(std::move(buffer));
}

void Console::Clear() {
  // We write directly instead of using Output because WriteToStdout expects
  // to append '\n' to outputs and won't flush it explicitly otherwise.
  line_input_.Hide();
  const char ff[] = "\033c";   // Form feed.
  write(STDOUT_FILENO, ff, sizeof(ff));
  line_input_.Show();
}

Console::Result Console::DispatchInputLine(const std::string& line,
                                           CommandCallback callback) {
  Command cmd;
  Err err;
  if (line.empty()) {
    // Repeat the previous command, don't add to history.
    cmd = previous_command_;
  } else {
    line_input_.AddToHistory(line);
    err = ParseCommand(line, &cmd);
  }

  if (err.ok()) {
    if (cmd.verb() == Verb::kQuit) {
      return Result::kQuit;
    } else {
      err = context_.FillOutCommand(&cmd);
      if (!err.has_error()) {
        err = DispatchCommand(&context_, cmd, callback);
        previous_command_ = cmd;

        if (cmd.thread() && cmd.verb() != Verb::kNone) {
          // Show the right source/disassembly for the next listing.
          context_.SetSourceAffinityForThread(
              cmd.thread(), GetVerbRecord(cmd.verb())->source_affinity);
        }
      }
    }
  }

  if (err.has_error()) {
    OutputBuffer out;
    out.OutputErr(err);
    Output(std::move(out));
  }
  return Result::kContinue;
}

Console::Result Console::ProcessInputLine(const std::string& line,
                                          CommandCallback callback) {
  Result result = DispatchInputLine(line, callback);
  if (result == Result::kQuit) {
    debug_ipc::MessageLoop::Current()->QuitNow();
  }
  return result;
}

void Console::OnFDReadable(int fd) {
  char ch;
  while (read(STDIN_FILENO, &ch, 1) > 0) {
    if (line_input_.OnInput(ch)) {
      Result result = ProcessInputLine(line_input_.line());
      if (result == Result::kQuit)
        return;
      line_input_.BeginReadLine();
    }
  }
}

}  // namespace zxdb
