// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/print_command_utils.h"

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/format_node_console.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

constexpr int kPrintCommandSwitchBase = 1000000;

constexpr int kVerboseFormat = kPrintCommandSwitchBase + 0;
constexpr int kForceAllTypes = kPrintCommandSwitchBase + 1;
constexpr int kForceNumberChar = kPrintCommandSwitchBase + 2;
constexpr int kForceNumberSigned = kPrintCommandSwitchBase + 3;
constexpr int kForceNumberUnsigned = kPrintCommandSwitchBase + 4;
constexpr int kForceNumberHex = kPrintCommandSwitchBase + 5;
constexpr int kMaxArraySize = kPrintCommandSwitchBase + 6;
constexpr int kRawOutput = kPrintCommandSwitchBase + 7;

}  // namespace

void AppendPrintCommandSwitches(VerbRecord* record) {
  record->switches.emplace_back(kForceAllTypes, false, "types", 't');
  record->switches.emplace_back(kRawOutput, false, "raw", 'r');
  record->switches.emplace_back(kVerboseFormat, false, "verbose", 'v');
  record->switches.emplace_back(kForceNumberChar, false, "", 'c');
  record->switches.emplace_back(kForceNumberSigned, false, "", 'd');
  record->switches.emplace_back(kForceNumberUnsigned, false, "", 'u');
  record->switches.emplace_back(kForceNumberHex, false, "", 'x');
  record->switches.emplace_back(kMaxArraySize, true, "max-array");
}

ErrOr<ConsoleFormatOptions> GetPrintCommandFormatOptions(const Command& cmd) {
  ConsoleFormatOptions options;

  // These defaults currently don't have exposed options. A pointer expand depth of one allows
  // local variables and "this" to be expanded without expanding anything else. Often pointed-to
  // classes are less useful and can be very large.
  options.pointer_expand_depth = 1;
  options.max_depth = 16;

  // All current users of this want the smart form.
  //
  // This keeps the default wrap columns at 80. We can consider querying the actual console width.
  // But very long lines start putting many struct members on the same line which gets increasingly
  // difficult to read. 80 columns feels reasonably close to how much you can take in at once.
  //
  // Note also that this doesn't stricly wrap the output to 80 columns. Long type names or values
  // will still use the full width and will be wrapped by the console. This wrapping only affects
  // the splitting of items across lines.
  options.wrapping = ConsoleFormatOptions::Wrapping::kSmart;

  // Verbosity.
  if (cmd.HasSwitch(kForceAllTypes))
    options.verbosity = ConsoleFormatOptions::Verbosity::kAllTypes;
  else if (cmd.HasSwitch(kVerboseFormat))
    options.verbosity = ConsoleFormatOptions::Verbosity::kMedium;
  else
    options.verbosity = ConsoleFormatOptions::Verbosity::kMinimal;

  // Array size.
  if (cmd.HasSwitch(kMaxArraySize)) {
    int size = 0;
    if (Err err = StringToInt(cmd.GetSwitchValue(kMaxArraySize), &size); err.has_error())
      return err;
    options.max_array_size = static_cast<uint32_t>(size);
  }

  // Mapping from command-line parameter to format enum.
  constexpr size_t kFormatCount = 4;
  static constexpr std::pair<int, ConsoleFormatOptions::NumFormat> kFormats[kFormatCount] = {
      {kForceNumberChar, ConsoleFormatOptions::NumFormat::kChar},
      {kForceNumberUnsigned, ConsoleFormatOptions::NumFormat::kUnsigned},
      {kForceNumberSigned, ConsoleFormatOptions::NumFormat::kSigned},
      {kForceNumberHex, ConsoleFormatOptions::NumFormat::kHex}};

  int num_type_overrides = 0;
  for (const auto& cur : kFormats) {
    if (cmd.HasSwitch(cur.first)) {
      num_type_overrides++;
      options.num_format = cur.second;
    }
  }

  // Disable pretty-printing.
  if (cmd.HasSwitch(kRawOutput))
    options.enable_pretty_printing = false;

  if (num_type_overrides > 1)
    return Err("More than one type override (-c, -d, -u, -x) specified.");
  return options;
}

}  // namespace zxdb
