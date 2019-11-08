// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/firebase_auth/oauth_request_builder.h"

#include <iomanip>
#include <iostream>

#include "src/lib/fsl/socket/strings.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

namespace firebase_auth {

namespace http = ::fuchsia::net::oldhttp;

namespace {

std::string UrlEncode(const std::string& value) {
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;

  for (char c : value) {
    // Keep alphanumeric and other accepted characters intact
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '=' || c == '&' || c == '+') {
      escaped << c;
      continue;
    }

    // Any other characters are percent-encoded
    escaped << std::uppercase;
    escaped << '%' << std::setw(2) << static_cast<unsigned int>(c);
    escaped << std::nouppercase;
  }

  return escaped.str();
}

constexpr char kFirebaseAuthEndpoint[] =
    "https://www.googleapis.com/identitytoolkit/v3/relyingparty/"
    "verifyAssertion";

}  // namespace

OAuthRequestBuilder::OAuthRequestBuilder(const std::string& url, const std::string& method)
    : url_(url), method_(method) {
  FXL_CHECK(!url_.empty());
  FXL_CHECK(!method_.empty());
}

OAuthRequestBuilder::~OAuthRequestBuilder() {}

OAuthRequestBuilder& OAuthRequestBuilder::SetJsonBody(const std::string& body) {
  http_headers_["accept"] = "application/json";
  http_headers_["content-type"] = "application/json";
  return SetRequestBody(body);
}

OAuthRequestBuilder& OAuthRequestBuilder::SetQueryParams(
    std::map<std::string, std::string> query_params) {
  query_string_.clear();
  for (auto it = query_params.begin(); it != query_params.end(); ++it) {
    query_string_ += (it == query_params.begin() ? "?" : "&");
    query_string_ += UrlEncode(it->first) + "=" + UrlEncode(it->second);
  }
  return *this;
}

http::URLRequest OAuthRequestBuilder::Build() const {
  fsl::SizedVmo data;
  auto result = fsl::VmoFromString(request_body_, &data);
  FXL_CHECK(result);

  http::URLRequest request;
  request.url = url_ + query_string_;
  request.method = method_;
  request.auto_follow_redirects = true;
  request.body = http::URLBody::New();
  request.body->set_buffer(std::move(data).ToTransport());
  for (const auto& http_header : http_headers_) {
    http::HttpHeader hdr;
    hdr.name = http_header.first;
    hdr.value = http_header.second;
    request.headers.emplace({std::move(hdr)});
  }

  return request;
}

OAuthRequestBuilder& OAuthRequestBuilder::SetRequestBody(const std::string& body) {
  request_body_ = body;

  uint64_t data_size = request_body_.length();
  if (data_size > 0)
    http_headers_["content-length"] = fxl::NumberToString(data_size).data();

  return *this;
}

FirebaseRequestBuilder::FirebaseRequestBuilder(std::string firebase_api_key,
                                               std::string google_id_token)
    : oauth_request_(OAuthRequestBuilder(kFirebaseAuthEndpoint, "POST")) {
  FXL_CHECK(!firebase_api_key.empty());
  FXL_CHECK(!google_id_token.empty());

  std::map<std::string, std::string> query_params;
  query_params["key"] = firebase_api_key;
  oauth_request_.SetQueryParams(query_params)
      .SetJsonBody(R"({"postBody": "id_token=)" + google_id_token + R"(&providerId=google.com",)" +
                   R"("returnIdpCredential": true,)" + R"("returnSecureToken": true,)" +
                   R"("requestUri": "http://localhost"})");
}

FirebaseRequestBuilder::~FirebaseRequestBuilder() {}

::fuchsia::net::oldhttp::URLRequest FirebaseRequestBuilder::Build() const {
  return oauth_request_.Build();
}

}  // namespace firebase_auth
