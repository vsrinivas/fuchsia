// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_IDENTITY_LIB_OAUTH_OAUTH_RESPONSE_H_
#define SRC_IDENTITY_LIB_OAUTH_OAUTH_RESPONSE_H_

#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/net/oldhttp/cpp/fidl.h>

#include "rapidjson/document.h"

namespace auth_providers {
namespace oauth {

struct OAuthResponse {
  const fuchsia::auth::AuthProviderStatus status;
  const std::string error_description;
  rapidjson::Document json_response;

  OAuthResponse(const fuchsia::auth::AuthProviderStatus& status,
                const std::string& error_description,
                rapidjson::Document json_response)
      : status(status),
        error_description(error_description),
        json_response(std::move(json_response)) {}
};

OAuthResponse ParseOAuthResponse(::fuchsia::net::oldhttp::URLResponse response);

}  // namespace oauth
}  // namespace auth_providers

#endif  // SRC_IDENTITY_LIB_OAUTH_OAUTH_RESPONSE_H_
