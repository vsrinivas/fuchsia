// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/google_analytics_client.h"

#include <lib/fit/bridge.h>
#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/client/curl.h"
#include "src/lib/fxl/strings/substitute.h"

namespace zxdb {

namespace {

std::shared_ptr<Curl> PrepareCurl(std::string_view user_agent) {
  auto curl = Curl::MakeShared();

  curl->SetURL(GoogleAnalyticsClient::kEndpoint);
  curl->headers().emplace_back(fxl::Substitute("User-Agent: $0", user_agent));

  return curl;
}

bool IsResponseCodeSuccess(long response_code) {
  return response_code >= 200 && response_code < 300;
}

fit::promise<void, GoogleAnalyticsNetError> CurlPerformAsync(const std::shared_ptr<Curl>& curl) {
  FX_DCHECK(curl);

  fit::bridge<void, GoogleAnalyticsNetError> bridge;
  curl->Perform([completer = std::move(bridge.completer)](Curl* curl, Curl::Error result) mutable {
    auto response_code = curl->ResponseCode();
    if (!result && IsResponseCodeSuccess(response_code)) {
      completer.complete_ok();
    } else if (result) {
      completer.complete_error(GoogleAnalyticsNetError(
          GoogleAnalyticsNetErrorType::kConnectionError, result.ToString()));
    } else {
      completer.complete_error(GoogleAnalyticsNetError(
          GoogleAnalyticsNetErrorType::kUnexpectedResponseCode, std::to_string(response_code)));
    }
  });
  return bridge.consumer.promise_or(
      fit::error(GoogleAnalyticsNetError(GoogleAnalyticsNetErrorType::kAbandoned)));
}

}  // namespace

void GoogleAnalyticsClient::CurlGlobalInit() {
  auto status = curl_global_init(CURL_GLOBAL_ALL);
  FX_DCHECK(status == CURLE_OK);
}

void GoogleAnalyticsClient::CurlGlobalCleanup() { curl_global_cleanup(); }

fit::promise<void, GoogleAnalyticsNetError> GoogleAnalyticsClient::SendData(
    std::string_view user_agent, const std::map<std::string, std::string>& parameters) const {
  auto curl = PrepareCurl(user_agent);
  curl->set_post_data(parameters);
  return CurlPerformAsync(curl);
}

}  // namespace zxdb
