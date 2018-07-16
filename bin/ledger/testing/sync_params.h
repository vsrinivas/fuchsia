// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_SYNC_PARAMS_H_
#define PERIDOT_BIN_LEDGER_TESTING_SYNC_PARAMS_H_

#include <lib/fxl/command_line.h>
#include <lib/fxl/strings/string_view.h>

namespace test {
namespace benchmark {

// Returns a string listing the command-line parameters which need to be
// provided for a benchmark to connect to a cloud server.
std::string GetSyncParamsUsage();

// Reads the sync parameters from the command-line. Prints a warning and returns
// false if these parameters are missing or cannot be parsed.
bool ParseSyncParamsFromCommandLine(fxl::CommandLine* command_line,
                                    std::string* server_id);

}  // namespace benchmark
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTING_SYNC_PARAMS_H_
