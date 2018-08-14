// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_SYNC_PARAMS_H_
#define PERIDOT_BIN_LEDGER_TESTING_SYNC_PARAMS_H_

#include <set>
#include <string>

#include <lib/fxl/command_line.h>
#include <lib/fxl/strings/string_view.h>

namespace ledger {

// Parameters needed to configure synchronization against a real server.
struct SyncParams {
  // ID of the Firestore instance.
  std::string server_id;

  // API key used to access the database.
  std::string api_key;

  // Content of the service account JSON credentials.
  std::string credentials;
};

// Returns a string listing the command-line parameters which need to be
// provided for a benchmark to connect to a cloud server.
std::string GetSyncParamsUsage();

// Reads the sync parameters from the command-line. Prints a warning and returns
// false if these parameters are missing or cannot be parsed.
bool ParseSyncParamsFromCommandLine(const fxl::CommandLine& command_line,
                                    SyncParams* sync_params);

// Returns the names of the flags parsed from the command line by
// ParseSyncParamsFromCommandLine(), without the leading "--".
std::set<std::string> GetSyncParamFlags();

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_SYNC_PARAMS_H_
