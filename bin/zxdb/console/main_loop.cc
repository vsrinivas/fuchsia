// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/main_loop.h"

#include <string>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_parser.h"
#include "garnet/bin/zxdb/console/line_input.h"
#include "garnet/bin/zxdb/console/output_buffer.h"

namespace zxdb {

namespace {

std::vector<std::string> CompletionCallback(const std::string& line) {
  return GetCommandCompletions(line);
}

}  // namespace

void RunMainLoop(Session* session) {
  LineInputBlockingStdio line_input("[zxdb] ");
  line_input.set_completion_callback(&CompletionCallback);

  FileOutputBuffer out(stdout);
  while (true) {
    std::string input = line_input.ReadLine();

    Command cmd;
    Err err = ParseCommand(input, &cmd);

    if (err.has_error()) {
      out.OutputErr(err);
    } else if (cmd.noun == Noun::kZxdb && cmd.verb == Verb::kQuit) {
      return;
    } else if (cmd.noun != Noun::kNone) {
      err = DispatchCommand(session, cmd, &out);
      if (err.has_error())
        out.OutputErr(err);
    }
  }
}

}  // namespace zxdb
