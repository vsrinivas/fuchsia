// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/main_loop.h"

#include <linenoise/linenoise.h>

#include <string>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_parser.h"
#include "garnet/bin/zxdb/console/output_buffer.h"

namespace zxdb {

namespace {

void CompletionCallback(const char* line, linenoiseCompletions* completions) {
  for (const auto& str : GetCommandCompletions(line))
    linenoiseAddCompletion(completions, str.c_str());
}

}  // namespace

void RunMainLoop(Session* session) {
  linenoiseSetCompletionCallback(&CompletionCallback);
  linenoiseHistorySetMaxLen(256);

  FileOutputBuffer out(stdout);
  while (true) {
    char* line = linenoise("[zxdb] ");
    if (!line)
      break;
    linenoiseHistoryAdd(line);
    std::string input(line);
    linenoiseFree(line);

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
