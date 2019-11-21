// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/sync_params.h"

#include <fuchsia/net/oldhttp/cpp/fidl.h>

#include <iostream>

#include <openssl/sha.h>

#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/firebase_auth/testing/credentials.h"
#include "src/lib/files/file.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/strings/string_view.h"
#include "src/lib/json_parser/rapidjson_validation.h"

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
constexpr fxl::StringView kGnCredentialsPathArg = "ledger_sync_credentials_file";
constexpr fxl::StringView kCredentialsDefaultPath = "/pkg/data/sync_credentials.json";

// URL that the sync infra bots use to pass the sync credentials to the tests.
constexpr fxl::StringView kCredentialsFetchUrl = "http://10.0.2.2:8081/ledger_e2e_sync_credentials";

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
bool FetchCredentials(sys::ComponentContext* component_context, std::string* credentials_path,
                      std::string* credentials) {
  *credentials_path = kCredentialsFetchUrl.ToString();

  fidl::SynchronousInterfacePtr<http::HttpService> network_service;
  component_context->svc()->Connect(network_service.NewRequest());
  fidl::SynchronousInterfacePtr<http::URLLoader> url_loader;

  zx_status_t status = network_service->CreateURLLoader(url_loader.NewRequest());
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "Unable to retrieve an URLLoader.";
    return false;
  }

  http::URLRequest request;
  request.method = "GET";
  request.url = kCredentialsFetchUrl.ToString();
  request.response_body_mode = http::ResponseBodyMode::BUFFER;
  http::URLResponse response;

  status = url_loader->Start(std::move(request), &response);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "Unable to start the network request.";
    return false;
  }

  if (response.error) {
    FXL_LOG(ERROR) << "Net error " << response.error->code << ": " << response.error->description;
    return false;
  }

  if (response.status_code != 200) {
    FXL_LOG(ERROR) << "Unexpected HTTP status code: " << response.status_code;
    return false;
  }

  return fsl::StringFromVmo(response.body->buffer(), credentials);
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
                           sys::ComponentContext* component_context, std::string* credentials_path,
                           std::string* credentials) {
  if (command_line.GetOptionValue(kCredentialsPathFlag.ToString(), credentials_path)) {
    return files::ReadFileToString(*credentials_path, credentials);
  }
  *credentials_path = kCredentialsDefaultPath.ToString();
  if (files::IsFile(*credentials_path)) {
    return files::ReadFileToString(*credentials_path, credentials);
  }

  return FetchCredentials(component_context, credentials_path, credentials);
}

std::string Hash(fxl::StringView data) {
  char result[SHA256_DIGEST_LENGTH];
  SHA256_CTX sha256;
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, data.data(), data.size());
  SHA256_Final(reinterpret_cast<uint8_t*>(result), &sha256);
  return convert::ToHex(fxl::StringView(result, SHA256_DIGEST_LENGTH));
}

}  // namespace

namespace ledger {

SyncParams::SyncParams() = default;

SyncParams::SyncParams(const SyncParams& other) { *this = other; }

SyncParams::SyncParams(SyncParams&& other) noexcept = default;

SyncParams& SyncParams::operator=(const SyncParams& other) {
  api_key = other.api_key;
  if (other.credentials) {
    credentials = other.credentials->Clone();
  }
  return *this;
}

SyncParams& SyncParams::operator=(SyncParams&& other) noexcept = default;

std::string GetSyncParamsUsage() {
  std::ostringstream result;
  result << " [--" << kCredentialsPathFlag << "=<file path>]";
  return result.str();
}

std::string ExtractJsonObject(const std::string& content) {
  auto start = content.find('{');
  auto end = content.rfind('}');
  if (start != std::string::npos && end != std::string::npos && start < end) {
    return content.substr(start, end + 1 - start);
  }
  return "";
}

bool ParseSyncParamsFromCommandLine(const fxl::CommandLine& command_line,
                                    sys::ComponentContext* component_context,
                                    SyncParams* sync_params) {
  std::string credentials;
  std::string credentials_path;
  if (!GetCredentialsContent(command_line, component_context, &credentials_path, &credentials)) {
    std::cerr << "Cannot access " << credentials_path << std::endl;
    WarnIncorrectSyncParams();
    return false;
  }

  FXL_LOG(INFO) << "Sync credentials sha256: " << Hash(credentials);

  rapidjson::Document document;
  document.Parse(credentials);
  if (document.HasParseError()) {
    std::cerr << "Cannot parse sync parameters at " << credentials_path << std::endl;
    // TODO(qsr): NET-1636 Remove this code once the network service handles
    // chunked encoding. Extract the content of credentials from the first '{'
    // to the last '}' to work around the network service not handling chunked
    // encoding.
    std::cerr << "Trying to extract a JSON object." << std::endl;
    credentials = ExtractJsonObject(credentials);
    if (credentials.empty()) {
      return false;
    }
    document.Parse(credentials);
    if (document.HasParseError()) {
      return false;
    }
  }
  auto sync_params_schema = json_parser::InitSchema(kSyncParamsSchema);
  FXL_DCHECK(sync_params_schema);
  if (!json_parser::ValidateSchema(document, *sync_params_schema)) {
    std::cerr << "Invalid schema at " << credentials_path << std::endl;
    return false;
  }

  sync_params->api_key = document["api-key"].GetString();
  sync_params->credentials = service_account::Credentials::Parse(document["service-account"]);
  if (!sync_params->credentials) {
    std::cerr << "Cannot parse credentials at " << credentials_path << std::endl;
    return false;
  }
  return true;
}

std::set<std::string> GetSyncParamFlags() { return {kCredentialsPathFlag.ToString()}; }

}  // namespace ledger
