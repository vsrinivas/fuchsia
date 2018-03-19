// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <iomanip>
#include <sstream>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/memory_dump.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/memory_format.h"
#include "garnet/bin/zxdb/console/output_buffer.h"

namespace zxdb {

namespace {

constexpr int kSizeSwitch = 1;

void MemoryReadComplete(const Err& err, MemoryDump dump) {
  OutputBuffer out;
  if (err.has_error()) {
    out.OutputErr(err);
  } else {
    MemoryFormatOptions opts;
    opts.show_addrs = true;
    opts.show_ascii = true;
    opts.values_per_line = 16;
    opts.separator_every = 8;
    out.Append(FormatMemory(dump, dump.address(),
                            static_cast<uint32_t>(dump.size()), opts));
  }
  Console::get()->Output(std::move(out));
}

const char kMemReadShortHelp[] =
    R"(mem-read / x: Read memory from debugged process.)";
const char kMemReadHelp[] =
    R"(mem-read

  Alias: "x"

  Reads memory from the process at the given address and prints it to the
  screen. Currently, only a byte-oriented hex dump format is supported.

Arguments

  --size
    Bytes to read. This defaults to 64 if unspecified.

Examples

  x --size=128 0x75f19ba
  mem-read --size=16 0x8f1763a7
  process 3 mem-read 83242384560
)";
Err DoMemRead(ConsoleContext* context, const Command& cmd) {
  // Only a process can have its memory read.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  err = AssertRunningTarget(context, "mem-read", cmd.target());
  if (err.has_error())
    return err;

  // Address (required).
  uint64_t address = 0;
  if (cmd.args().size() != 1) {
    return Err(ErrType::kInput, "mem-read requires exactly one argument that "
        "is the address to read.");
  }
  err = StringToUint64(cmd.args()[0], &address);
  if (err.has_error())
    return err;

  // Size argument (optional).
  uint64_t size = 64;
  if (cmd.HasSwitch(kSizeSwitch)) {
    err = StringToUint64(cmd.GetSwitchValue(kSizeSwitch), &size);
    if (err.has_error())
      return err;
  }

  cmd.target()->GetProcess()->ReadMemory(address, size, &MemoryReadComplete);
  return Err();
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
