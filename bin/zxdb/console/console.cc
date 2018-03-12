// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/console.h"

#include <fcntl.h>
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

Console* Console::singleton_ = nullptr;

Console::Console(Session* session)
    : context_(session), line_input_("[zxdb] ") {
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
    if (cmd.verb() == Verb::kQuit) {
      return Result::kQuit;
    } else {
      err = context_.FillOutCommand(&cmd);
      if (!err.has_error())
        err = DispatchCommand(&context_, cmd);
    }
  }

  if (err.has_error()) {
    OutputBuffer out;
    out.OutputErr(err);
    Output(std::move(out));
  }
  return Result::kContinue;
}

}  // namespace zxdb
