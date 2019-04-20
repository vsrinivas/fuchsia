// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/identity/lib/oauth/oauth_request_builder.h"

#include <iomanip>
#include <iostream>

#include "lib/fsl/socket/strings.h"
#include "lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

namespace auth_providers {
namespace oauth {

namespace http = ::fuchsia::net::oldhttp;

namespace {

std::string UrlEncode(const std::string& value) {
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;

  for (char c : value) {
    // Keep alphanumeric and other accepted characters intact
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '=' ||
        c == '&' || c == '+') {
      escaped << c;
      continue;
    }

    // Any other characters are percent-encoded
    escaped << std::uppercase;
    escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
    escaped << std::nouppercase;
  }

  return escaped.str();
}

}  // namespace

OAuthRequestBuilder::OAuthRequestBuilder(const std::string& url,
                                         const std::string& method)
    : url_(url), method_(method) {
  FXL_CHECK(!url_.empty());
  FXL_CHECK(!method_.empty());
}

OAuthRequestBuilder::~OAuthRequestBuilder() {}

OAuthRequestBuilder& OAuthRequestBuilder::SetAuthorizationHeader(
    const std::string& token) {
  FXL_DCHECK(!token.empty());
  http_headers_["Authorization"] = "Bearer " + token;
  return *this;
}

OAuthRequestBuilder& OAuthRequestBuilder::SetUrlEncodedBody(
    const std::string& body) {
  http_headers_["content-type"] = "application/x-www-form-urlencoded";

  if (body.empty()) {
    return *this;
  }
  return SetRequestBody(UrlEncode(body));
}

OAuthRequestBuilder& OAuthRequestBuilder::SetJsonBody(const std::string& body) {
  http_headers_["accept"] = "application/json";
  http_headers_["content-type"] = "application/json";
  return SetRequestBody(body);
}

OAuthRequestBuilder& OAuthRequestBuilder::SetQueryParams(
    std::map<std::string, std::string> query_params) {
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
    request.headers.push_back(std::move(hdr));
  }

  return request;
}

OAuthRequestBuilder& OAuthRequestBuilder::SetRequestBody(
    const std::string& body) {
  request_body_ = body;

  uint64_t data_size = request_body_.length();
  if (data_size > 0)
    http_headers_["content-length"] = fxl::NumberToString(data_size).data();

  return *this;
}

}  // namespace oauth
}  // namespace auth_providers
