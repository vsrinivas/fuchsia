// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_aspace.h"

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

const char kAspaceShortHelp[] = "aspace / as: Show address space for a process.";
const char kAspaceHelp[] =
    R"(aspace [ <address> ]

  Alias: "as"

  Shows the address space map for the given process.

  With no parameters, it shows the entire process address map.
  You can pass a single address and it will show all the regions that
  contain it.

Examples

  aspace
  aspace 0x530b010dc000
  process 2 aspace
)";

std::string PrintRegionSize(uint64_t size) {
  const uint64_t kOneK = 1024u;
  const uint64_t kOneM = kOneK * kOneK;
  const uint64_t kOneG = kOneM * kOneK;
  const uint64_t kOneT = kOneG * kOneK;

  if (size < kOneK)
    return fxl::StringPrintf("%" PRIu64 "B", size);
  if (size < kOneM)
    return fxl::StringPrintf("%" PRIu64 "K", size / kOneK);
  if (size < kOneG)
    return fxl::StringPrintf("%" PRIu64 "M", size / kOneM);
  if (size < kOneT)
    return fxl::StringPrintf("%" PRIu64 "G", size / kOneG);
  return fxl::StringPrintf("%" PRIu64 "T", size / kOneT);
}

std::string PrintRegionName(uint64_t depth, const std::string& name) {
  return std::string(depth * 2, ' ') + name;
}

void OnAspaceComplete(const Err& err, std::vector<debug_ipc::AddressRegion> map) {
  Console* console = Console::get();
  if (err.has_error()) {
    console->Output(err);
    return;
  }

  if (map.empty()) {
    console->Output("Region not mapped.");
    return;
  }

  std::vector<std::vector<std::string>> rows;
  for (const auto& region : map) {
    rows.push_back(std::vector<std::string>{
        fxl::StringPrintf("0x%" PRIx64, region.base),
        fxl::StringPrintf("0x%" PRIx64, region.base + region.size), PrintRegionSize(region.size),
        PrintRegionName(region.depth, region.name)});
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kRight, 0, "Start", 2), ColSpec(Align::kRight, 0, "End", 2),
               ColSpec(Align::kRight, 0, "Size", 2), ColSpec(Align::kLeft, 0, "Name", 1)},
              rows, &out);

  console->Output(out);
}

Err RunVerbAspace(ConsoleContext* context, const Command& cmd) {
  // Only a process can be specified.
  if (Err err = cmd.ValidateNouns({Noun::kProcess}); err.has_error())
    return err;

  uint64_t address = 0;
  if (cmd.args().size() == 1) {
    if (Err err = ReadUint64Arg(cmd, 0, "address", &address); err.has_error())
      return err;
  } else if (cmd.args().size() > 1) {
    return Err(ErrType::kInput, "\"aspace\" takes zero or one parameter.");
  }

  if (Err err = AssertRunningTarget(context, "aspace", cmd.target()); err.has_error())
    return err;

  cmd.target()->GetProcess()->GetAspace(address, &OnAspaceComplete);
  return Err();
}

}  // namespace

VerbRecord GetAspaceVerbRecord() {
  return VerbRecord(&RunVerbAspace, {"aspace", "as"}, kAspaceShortHelp, kAspaceHelp,
                    CommandGroup::kQuery);
}

}  // namespace zxdb
