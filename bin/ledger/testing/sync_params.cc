// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/sync_params.h"

#include <iostream>

namespace {

constexpr fxl::StringView kServerIdFlag = "server-id";

void WarnIncorrectSyncParams() {
  std::cout << "Missing " << kServerIdFlag << " parameter." << std::endl;
  std::cout << "This benchmark needs an ID of a configured Firebase instance "
               "to run. If you're running it from a .tspec file, make sure "
               "you add --append-args=\"--"
            << kServerIdFlag << "=<string>\"." << std::endl;
}

}  // namespace

namespace test {
namespace benchmark {

std::string GetSyncParamsUsage() {
  std::ostringstream result;
  result << " --" << kServerIdFlag << "=<string>";
  return result.str();
}

bool ParseSyncParamsFromCommandLine(fxl::CommandLine* command_line,
                                    std::string* server_id) {
  bool ret = command_line->GetOptionValue(kServerIdFlag.ToString(), server_id);
  if (!ret) {
    WarnIncorrectSyncParams();
  }
  return ret;
}

}  // namespace benchmark
}  // namespace test
