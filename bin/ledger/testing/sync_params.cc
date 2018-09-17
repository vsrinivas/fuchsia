// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/sync_params.h"

#include <iostream>

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/files/file.h>

#include "peridot/lib/firebase_auth/testing/credentials.h"
#include "peridot/lib/firebase_auth/testing/json_schema.h"

namespace {

namespace http = ::fuchsia::net::oldhttp;

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

// URL that the sync infra bots use to pass the sync credentials to the tests.
constexpr fxl::StringView kCredentialsFetchUrl =
    "http://10.0.2.2:8081/ledger_e2e_sync_credentials";

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

// Fetches the sync credentials from the network. This method is synchronous and
// blocks until credentials are retrieved. This is intended exclusively for
// infra bots that will expose the credentials over the network when running
// sync tests.
bool FetchCredentials(component::StartupContext* startup_context,
                      std::string* credentials_path, std::string* credentials) {
  *credentials_path = kCredentialsFetchUrl.ToString();

  fidl::SynchronousInterfacePtr<http::HttpService> network_service;
  startup_context->ConnectToEnvironmentService(network_service.NewRequest());
  fidl::SynchronousInterfacePtr<http::URLLoader> url_loader;

  zx_status_t status =
      network_service->CreateURLLoader(url_loader.NewRequest());
  if (status != ZX_OK) {
    return false;
  }

  http::URLRequest request;
  request.method = "GET";
  request.url = kCredentialsFetchUrl.ToString();
  request.response_body_mode = http::ResponseBodyMode::SIZED_BUFFER;
  http::URLResponse response;

  status = url_loader->Start(std::move(request), &response);
  if (status != ZX_OK) {
    return false;
  }

  if (response.error) {
    FXL_LOG(ERROR) << "Net error " << response.error->code << ": "
                   << response.error->description;
    return false;
  }

  if (response.status_code != 200) {
    FXL_LOG(ERROR) << "Unexpected HTTP status code: " << response.status_code;
    return false;
  }

  return fsl::StringFromVmo(response.body->sized_buffer(), credentials);
}

// Extracts the credentials content. This function will return |true| if it
// finds the credentials in either:
// - The command line
// - The default location in the running package
// - Over the network.
// In that case, |credentials| will contain the content of the credentials file
// and |credentials_path| the path to the file.
// If it cannot find the credentials, this function will return |false|, and
// |credentials_path| will contain the path of the last tried location.
bool GetCredentialsContent(const fxl::CommandLine& command_line,
                           component::StartupContext* startup_context,
                           std::string* credentials_path,
                           std::string* credentials) {
  if (command_line.GetOptionValue(kCredentialsPathFlag.ToString(),
                                  credentials_path)) {
    return files::ReadFileToString(*credentials_path, credentials);
  }
  *credentials_path = kCredentialsDefaultPath.ToString();
  if (files::IsFile(*credentials_path)) {
    return files::ReadFileToString(*credentials_path, credentials);
  }

  return FetchCredentials(startup_context, credentials_path, credentials);
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
  std::string credentials_path;
  if (!GetCredentialsContent(command_line, startup_context, &credentials_path,
                             &credentials)) {
    std::cerr << "Cannot access " << credentials_path << std::endl;
    WarnIncorrectSyncParams();
    return false;
  }

  rapidjson::Document document;
  document.Parse(credentials);
  if (document.HasParseError()) {
    std::cerr << "Cannot parse credentials at " << credentials_path
              << std::endl;
    return false;
  }
  auto sync_params_schema = json_schema::InitSchema(kSyncParamsSchema);
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
