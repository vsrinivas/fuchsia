// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/output_buffer.h"

namespace zxdb {

namespace {

// process ---------------------------------------------------------------------

const char kProcessHelp[] =
    R"(process <verb>

Alias: "pro"

    With no argument, prints out the current debugged process information.
    )";
Err DoProcess(Session* session, const Command& cmd) {
  return Err("Unimplemented");
}

// process attach --------------------------------------------------------------

const char kProcessAttachHelp[] =
    R"(process attach <koid>

    Attach to the given process.
    )";
Err DoProcessAttach(Session* session, const Command& cmd) {
  return Err("Unimplemented");
}

// process list ----------------------------------------------------------------

const char kProcessListHelp[] =
    R"(process list

    Lists all debugged processes.
    )";
Err DoProcessList(Session* session, const Command& cmd) {
  return Err("Unimplemented");
}

// process run -----------------------------------------------------------------

const char kProcessRunHelp[] =
    R"(process run

    Aliases: "run", "r"
    )";

Err DoProcessRun(Session* session, const Command& cmd) {
  return Err("Unimplemented");
}

}  // namespace

std::map<Verb, CommandRecord> GetProcessVerbs() {
  std::map<Verb, CommandRecord> map;
  map[Verb::kNone] = CommandRecord(&DoProcess, kProcessHelp);
  map[Verb::kAttach] = CommandRecord(&DoProcessAttach, kProcessAttachHelp);
  map[Verb::kList] = CommandRecord(&DoProcessList, kProcessListHelp);
  map[Verb::kRun] = CommandRecord(&DoProcessRun, kProcessRunHelp);
  return map;
}

}  // namespace zxdb
