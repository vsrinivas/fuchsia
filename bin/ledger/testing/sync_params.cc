// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/sync_params.h"

#include <iostream>

#include <lib/fxl/files/file.h>

namespace {

constexpr fxl::StringView kServerIdFlag = "server-id";
constexpr fxl::StringView kApiKeyFlag = "api-key";
constexpr fxl::StringView kCredentialsPathFlag = "credentials-path";
constexpr fxl::StringView kGnCredentialsPathArg =
    "ledger_sync_credentials_file";
constexpr fxl::StringView kCredentialsDefaultPath =
    "/pkg/data/sync_credentials.json";

void WarnIncorrectSyncParams() {
  std::cerr << "Missing one or more of the sync parameters." << std::endl;
  std::cerr << "This binary needs an ID of a configured Firestore instance "
               "to run along with access credentials. "
               "If you're running it from a .tspec file, make sure "
               "you add --append-args=\""
            << "--" << kServerIdFlag << "=<string>,"
            << "--" << kApiKeyFlag << "=<string>,"
            << "\"." << std::endl;
  std::cerr << "You can also pass the "
            << "--" << kCredentialsPathFlag << "=<file path>"
            << "if the access credentials are not embedded in the binary "
            << "at build." << std::endl;
}

}  // namespace

namespace ledger {

std::string GetSyncParamsUsage() {
  std::ostringstream result;
  result << " --" << kServerIdFlag << "=<string>";
  result << " --" << kApiKeyFlag << "=<string>";
  result << " [--" << kCredentialsPathFlag << "=<file path>]";
  return result.str();
}

bool ParseSyncParamsFromCommandLine(const fxl::CommandLine& command_line,
                                    SyncParams* sync_params) {
  std::string credentials_path;
  bool ret = command_line.GetOptionValue(kServerIdFlag.ToString(),
                                         &sync_params->server_id) &&
             command_line.GetOptionValue(kApiKeyFlag.ToString(),
                                         &sync_params->api_key);
  if (!ret) {
    WarnIncorrectSyncParams();
    return false;
  }

  if (!files::ReadFileToString(kCredentialsDefaultPath.ToString(),
                               &sync_params->credentials)) {
    std::cerr << "Cannot access the default credentials location: "
              << kCredentialsDefaultPath << std::endl;

    std::string credentials_path;
    if (!command_line.GetOptionValue(kCredentialsPathFlag.ToString(),
                                     &credentials_path)) {
      std::cerr << "Please set the GN argument " << kGnCredentialsPathArg
                << " at build time to embed the credentials in the binary "
                << " or pass " << kCredentialsPathFlag
                << " at run time to override the default location" << std::endl;
      return false;
    }

    if (!files::ReadFileToString(credentials_path, &sync_params->credentials)) {
      std::cerr << "Cannot access " << credentials_path << std::endl;
      return false;
    }
  }
  return true;
}

std::set<std::string> GetSyncParamFlags() {
  return {kServerIdFlag.ToString(), kApiKeyFlag.ToString(),
          kCredentialsPathFlag.ToString()};
}

}  // namespace ledger
