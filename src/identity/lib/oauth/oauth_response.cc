// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/identity/lib/oauth/oauth_response.h"

#include "lib/fsl/socket/strings.h"
#include "rapidjson/error/en.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"

namespace auth_providers {
namespace oauth {

namespace http = ::fuchsia::net::oldhttp;

using fuchsia::auth::AuthProviderStatus;

OAuthResponse ParseOAuthResponse(http::URLResponse response) {
  rapidjson::Document out;
  if (response.error) {
    FXL_LOG(ERROR) << "Encountered error: " +
                          std::to_string(response.error->code) +
                          " ,with description: " +
                          response.error->description->data();
    return OAuthResponse(AuthProviderStatus::NETWORK_ERROR,
                         response.error->description->data(), std::move(out));
  }

  std::string response_body;
  if (response.body) {
    FXL_DCHECK(response.body->is_stream());
    if (!fsl::BlockingCopyToString(std::move(response.body->stream()),
                                   &response_body)) {
      FXL_LOG(ERROR) << "Internal error while reading response from socket,"
                        "network returned: " +
                            std::to_string(response.status_code);
      return OAuthResponse(AuthProviderStatus::NETWORK_ERROR,
                           "Error reading response from socket",
                           std::move(out));
    }
  }

  // OAuth errors are sent in the response body, parse the json response first
  // to introspect the response.
  rapidjson::ParseResult ok = out.Parse(response_body);
  if (!ok) {
    std::string error_msg = GetParseError_En(ok.Code());
    return OAuthResponse(
        AuthProviderStatus::BAD_RESPONSE,
        "Error in parsing json response[" + response_body + "]: " + error_msg,
        std::move(out));
  }
  switch (response.status_code) {
    case 200:  // Success
      return OAuthResponse(AuthProviderStatus::OK, "", std::move(out));
    case 400:  // Bad request errors
    case 401:  // Unauthorized, returned with invalid_client.
    case 403:  // Forbidden, user denied access.
    default:
      std::string oauth_error(out.HasMember("error") && out["error"].IsString()
                                  ? out["error"].GetString()
                                  : "");
      auto status = (oauth_error == "invalid_grant")
                        ? AuthProviderStatus::REAUTH_REQUIRED
                        : AuthProviderStatus::OAUTH_SERVER_ERROR;

      return OAuthResponse(status,
                           "OAuth backend returned error: " +
                               std::to_string(response.status_code),
                           std::move(out));
  }
}

}  // namespace oauth
}  // namespace auth_providers
