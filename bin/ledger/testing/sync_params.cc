// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/sync_params.h"

#include <iostream>

#include <lib/fxl/files/file.h>

#include "peridot/lib/firebase_auth/testing/credentials.h"
#include "peridot/lib/firebase_auth/testing/json_schema.h"

namespace {

constexpr fxl::StringView kSyncParamsSchema = R"({
  "type": "object",
  "properties": {
    "api-key": {
      "type": "string"
    },
    "service-account": {
      "type": "object"
    }
  },
  "required": ["api-key", "service-account"]
})";

constexpr fxl::StringView kCredentialsPathFlag = "credentials-path";
constexpr fxl::StringView kGnCredentialsPathArg =
    "ledger_sync_credentials_file";
constexpr fxl::StringView kCredentialsDefaultPath =
    "/pkg/data/sync_credentials.json";

void WarnIncorrectSyncParams() {
  std::cerr << "Missing the sync parameters." << std::endl;
  std::cerr << "This binary needs an ID of a configured Firestore instance "
               "to run along with access credentials. "
            << std::endl;
  std::cerr << "Please set the GN argument " << kGnCredentialsPathArg
            << " at build time to embed the credentials in the binary "
            << " or pass " << kCredentialsPathFlag
            << " at run time to override the default location" << std::endl;
  std::cerr << "If you're running it from a .tspec file, make sure "
               "you add --append-args=\""
               "--"
            << kCredentialsPathFlag << "=<file path>" << std::endl;
  std::cerr << "if the access credentials are not embedded in the binary "
            << "at build." << std::endl;
}

}  // namespace

namespace ledger {

SyncParams::SyncParams() = default;

SyncParams::SyncParams(SyncParams&& other) = default;

SyncParams::SyncParams(const SyncParams& other) { *this = other; }

SyncParams& SyncParams::operator=(SyncParams&& other) = default;

SyncParams& SyncParams::operator=(const SyncParams& other) {
  api_key = other.api_key;
  if (other.credentials) {
    credentials = other.credentials->Clone();
  }
  return *this;
}

std::string GetSyncParamsUsage() {
  std::ostringstream result;
  result << " [--" << kCredentialsPathFlag << "=<file path>]";
  return result.str();
}

bool ParseSyncParamsFromCommandLine(const fxl::CommandLine& command_line,
                                    component::StartupContext* startup_context,
                                    SyncParams* sync_params) {
  std::string credentials;
  std::string credentials_path = kCredentialsDefaultPath.ToString();
  if (!files::ReadFileToString(credentials_path, &credentials)) {
    std::cerr << "Cannot access the default credentials location: "
              << kCredentialsDefaultPath << std::endl;

    if (!command_line.GetOptionValue(kCredentialsPathFlag.ToString(),
                                     &credentials_path)) {
      WarnIncorrectSyncParams();
      return false;
    }

    if (!files::ReadFileToString(credentials_path, &credentials)) {
      std::cerr << "Cannot access " << credentials_path << std::endl;
      return false;
    }
  }

  rapidjson::Document document;
  document.Parse(credentials);
  if (document.HasParseError()) {
    std::cerr << "Cannot parse credentials at " << credentials_path
              << std::endl;
    return false;
  }
  auto sync_params_schema =  json_schema::InitSchema(kSyncParamsSchema);
  if (!json_schema::ValidateSchema(document, *sync_params_schema)) {
    std::cerr << "Cannot parse credentials at " << credentials_path
              << std::endl;
    return false;
  }

  sync_params->api_key = document["api-key"].GetString();
  sync_params->credentials = service_account::Credentials::Parse(document["service-account"]);
  if (!sync_params->credentials) {
    std::cerr << "Cannot parse credentials at " << credentials_path
              << std::endl;
    return false;
  }
  return true;
}

std::set<std::string> GetSyncParamFlags() {
  return {kCredentialsPathFlag.ToString()};
}

}  // namespace ledger
