// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <iomanip>
#include <sstream>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/output_buffer.h"

namespace zxdb {

namespace {

constexpr int kSizeSwitch = 1;

const char kMemReadShortHelp[] =
    R"(mem-read / x: Read memory from debugged process.)";
const char kMemReadHelp[] =
    R"(mem-read

  Alias: "x"

Reads memory from debugged process.)";
Err DoMemRead(ConsoleContext* context, const Command& cmd) {
  return Err("Unimplemented");
}

}  // namespace

void AppendMemoryVerbs(std::map<Verb, VerbRecord>* verbs) {
  // "x" is the GDB command to read memory.
  VerbRecord mem_read(&DoMemRead, {"mem-read", "x"}, kMemReadShortHelp,
                      kMemReadHelp);
  mem_read.switches.push_back(SwitchRecord(kSizeSwitch, true, "size", 's'));
  (*verbs)[Verb::kMemRead] = std::move(mem_read);
}

}  // namespace zxdb
