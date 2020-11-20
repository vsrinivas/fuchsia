// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/result.h>
#include <stdio.h>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/message_loop_poll.h"
#include "src/developer/debug/zxdb/client/google_analytics_client.h"

using zxdb::GoogleAnalyticsClient;
using zxdb::GoogleAnalyticsEvent;
using zxdb::GoogleAnalyticsNetError;
using zxdb::GoogleAnalyticsNetErrorType;

namespace {

int ProcessAddEventResult(const fit::result<void, GoogleAnalyticsNetError>& result) {
  int ret = 1;
  if (result.is_ok()) {
    printf("AddEvent success!\n");
    ret = 0;
  } else {
    const auto& error = result.error();
    std::string error_type;
    switch (error.type()) {
      case GoogleAnalyticsNetErrorType::kConnectionError: {
        error_type = "Connection error";
        break;
      }
      case GoogleAnalyticsNetErrorType::kUnexpectedResponseCode: {
        error_type = "Unexpected response code";
        break;
      }
      case GoogleAnalyticsNetErrorType::kAbandoned: {
        error_type = "Abandoned";
        break;
      }
    }
    fprintf(stderr, "AddEvent failed: %s - %s\n", error_type.c_str(), error.details().c_str());
    ret = 1;
  }
  return ret;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <tracking-id> <client-id>\n", argv[0]);
    return 1;
  }

  std::string tracking_id(argv[1]);
  std::string client_id(argv[2]);

  GoogleAnalyticsClient::CurlGlobalInit();
  auto ga_client = GoogleAnalyticsClient();
  ga_client.SetTrackingId(tracking_id);
  ga_client.SetClientId(client_id);
  ga_client.SetUserAgent("Fuchsia-tools-lib-analytics");

  auto event = GoogleAnalyticsEvent("test event", "test", "test label", 12345);

  debug_ipc::MessageLoopPoll loop;
  std::string error_message;
  if (!loop.Init(&error_message)) {
    fprintf(stderr, "Message loop initialization error: %s\n", error_message.c_str());
    return 1;
  }

  // If ret is not set to 0 at the end, the program is not executed successfully.
  int ret = 1;
  // This scope forces all the objects to be destroyed before the Cleanup() call which will mark the
  // message loop as not-current.
  {
    auto p =
        ga_client.AddEvent(event).then([&ret](fit::result<void, GoogleAnalyticsNetError>& result) {
          ret = ProcessAddEventResult(result);
          debug_ipc::MessageLoop::Current()->QuitNow();
        });
    loop.schedule_task(std::move(p));

    loop.Run();
  }

  loop.Cleanup();
  GoogleAnalyticsClient::CurlGlobalCleanup();

  return ret;
}
