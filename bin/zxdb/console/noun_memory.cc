// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/output_buffer.h"

namespace zxdb {

namespace {

// Switch IDs for memory switches.
constexpr int kSwitchSize = 1;

// memory ----------------------------------------------------------------------

const char kMemoryHelp[] =
    R"(memory <verb>

Alias: "mem"
    )";

Err DoMemory(Session* session, const Command& cmd, OutputBuffer* out) {
  // "memory" by itself does nothing, print help.
  Command help_cmd;
  help_cmd.noun = Noun::kZxdb;
  help_cmd.verb = Verb::kHelp;
  help_cmd.args.push_back("memory");
  return DispatchCommand(session, help_cmd, out);
}

// memory read -----------------------------------------------------------------

const char kMemoryReadHelp[] =
    R"(memory read [--size|-s <bytes>] <address>

    Reads the memory at the given address.

    --size / -s
        Byte cound of memory to read. If unspecified it will default to 64.
    )";

Err DoMemoryRead(Session* session, const Command& cmd, OutputBuffer* out) {
  return Err("Unimplemented");
}

// memory write ----------------------------------------------------------------

const char kMemoryWriteHelp[] =
    R"(memory write

    Unimplemented.
    )";

Err DoMemoryWrite(Session* session, const Command& cmd, OutputBuffer* out) {
  return Err("Unimplemented");
}

}  // namespace

std::map<Verb, CommandRecord> GetMemoryVerbs() {
  std::map<Verb, CommandRecord> map;
  map[Verb::kNone] = CommandRecord(&DoMemory, kMemoryHelp);

  map[Verb::kRead] = CommandRecord(&DoMemoryRead, kMemoryReadHelp);
  map[Verb::kRead].switches.emplace_back(kSwitchSize, true, "size", 's');

  map[Verb::kWrite] = CommandRecord(&DoMemoryWrite, kMemoryWriteHelp);
  return map;
}

}  // namespace zxdb
