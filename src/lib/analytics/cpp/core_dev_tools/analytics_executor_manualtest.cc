// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is an integrated test to make sure libcurl works well with the threading and timeout
// logic implemented in AnalyticsExecutor.

#include <lib/fit/defer.h>
#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>

#include <iostream>
#include <string>

#include "src/developer/debug/zxdb/common/curl.h"
#include "src/lib/analytics/cpp/core_dev_tools/analytics_executor.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/lib/fxl/strings/substitute.h"

using ::analytics::core_dev_tools::AnalyticsExecutor;
using ::zxdb::Curl;

namespace {

bool IsResponseCodeSuccess(long response_code) {
  return response_code >= 200 && response_code < 300;
}

fpromise::promise<> CurlPerformAsync(const std::string& url, const std::string& data) {
  auto curl = fxl::MakeRefCounted<Curl>();

  curl->SetURL(url);
  auto* curl_version_data = curl_version_info(CURLVERSION_NOW);
  curl->headers().emplace_back(
      fxl::Substitute("User-Agent: libcurl/$0", curl_version_data->version));
  curl->set_post_data(data);
  curl->set_header_callback([](const std::string& data) {
    std::cout << data;
    return data.size();
  });
  curl->set_data_callback([](const std::string& data) {
    std::cout << data;
    return data.size();
  });

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

int main(int argc, char* argv[]) {
  if (argc != 4) {
    std::cerr << "Usage: " << argv[0] << " <soft-timeout-ms> <url> <data>" << std::endl;
    return 1;
  }

  int64_t soft_timeout_ms;
  bool success;
  success = fxl::StringToNumberWithError(argv[1], &soft_timeout_ms);
  if (!success) {
    std::cerr << argv[1] << " is not a valid int64_t" << std::endl;
    return 1;
  }

  std::string url(argv[2]);
  std::string data(argv[3]);

  debug_ipc::Curl::GlobalInit();
  auto deferred_cleanup = fit::defer(debug_ipc::Curl::GlobalCleanup);

  // This scope forces all the objects to be destroyed before the curl_global_cleanup call
  {
    AnalyticsExecutor executor(soft_timeout_ms);
    executor.schedule_task(
        fpromise::make_promise([&url, &data] { return CurlPerformAsync(url, data); }));
  }

  return 0;
}
