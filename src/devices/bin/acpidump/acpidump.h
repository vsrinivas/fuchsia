// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_ACPIDUMP_ACPIDUMP_H_
#define SRC_DEVICES_BIN_ACPIDUMP_ACPIDUMP_H_

#include <lib/stdcompat/span.h>

#include <optional>
#include <string>

#include <fbl/array.h>

namespace acpidump {

// Parsed command line arguments.
struct Args {
  // Table to dump. If empty, dump all tables.
  std::optional<std::string> table;

  // Should we dump raw binary data?
  bool dump_raw = false;

  // Should we show help?
  bool show_help = false;

  // Should we only show table names, but not content?
  bool table_names_only = false;
};

// Parse the given set of arguments.
//
// On success, fills in |result| with parsed arguments and returns true.
bool ParseArgs(cpp20::span<const char* const> args, Args* result);

// Print the given data in hex to the given file.
void PrintHex(const char* name, const fbl::Array<uint8_t>& data);

// Entry point.
int Main(int argc, const char** argv);

}  // namespace acpidump

#endif  // SRC_DEVICES_BIN_ACPIDUMP_ACPIDUMP_H_
