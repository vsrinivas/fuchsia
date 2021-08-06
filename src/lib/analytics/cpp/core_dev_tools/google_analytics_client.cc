// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/core_dev_tools/google_analytics_client.h"

#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/shared/curl.h"
#include "src/lib/fxl/strings/substitute.h"

namespace analytics::core_dev_tools {

using ::debug::Curl;

namespace {

fxl::RefPtr<Curl> PrepareCurl(std::string_view user_agent) {
  auto curl = fxl::MakeRefCounted<Curl>();

  curl->SetURL(GoogleAnalyticsClient::kEndpoint);
  curl->headers().emplace_back(fxl::Substitute("User-Agent: $0", user_agent));

  return curl;
}

bool IsResponseCodeSuccess(long response_code) {
  return response_code >= 200 && response_code < 300;
}

fpromise::promise<> CurlPerformAsync(const fxl::RefPtr<Curl>& curl) {
  FX_DCHECK(curl);

  fpromise::bridge bridge;
  curl->Perform([completer = std::move(bridge.completer)](Curl* curl, Curl::Error result) mutable {
    auto response_code = curl->ResponseCode();
    if (!result && IsResponseCodeSuccess(response_code)) {
      completer.complete_ok();
    } else {
      completer.complete_error();
    }
  });
  return bridge.consumer.promise();
}

}  // namespace

void GoogleAnalyticsClient::SendData(std::string_view user_agent,
                                     std::map<std::string, std::string> parameters) {
  executor_.schedule_task(fpromise::make_promise([user_agent, parameters{std::move(parameters)}] {
    auto curl = PrepareCurl(user_agent);
    curl->set_post_data(parameters);
    return CurlPerformAsync(curl);
  }));
}

}  // namespace analytics::core_dev_tools
