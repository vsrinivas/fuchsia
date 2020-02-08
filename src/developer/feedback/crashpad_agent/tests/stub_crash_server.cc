// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/tests/stub_crash_server.h"

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace feedback {

const char kStubCrashServerUrl[] = "localhost:1234";

const char kStubServerReportId[] = "server-report-id";

StubCrashServer::~StubCrashServer() {
  FXL_CHECK(!ExpectRequest()) << fxl::StringPrintf(
      "expected %ld more calls to MakeRequest() (%ld/%lu calls made)",
      std::distance(next_return_value_, request_return_values_.cend()),
      std::distance(request_return_values_.cbegin(), next_return_value_),
      request_return_values_.size());
}

bool StubCrashServer::MakeRequest(const std::map<std::string, std::string>& annotations,
                                  const std::map<std::string, crashpad::FileReader*>& attachments,
                                  std::string* server_report_id) {
  latest_annotations_ = annotations;
  latest_attachment_keys_.clear();
  for (const auto& [key, unused_value] : attachments) {
    latest_attachment_keys_.push_back(key);
  }

  FXL_CHECK(ExpectRequest()) << fxl::StringPrintf(
      "no more calls to MakeRequest() expected (%lu/%lu calls made)",
      std::distance(request_return_values_.cbegin(), next_return_value_),
      request_return_values_.size());
  if (*next_return_value_) {
    *server_report_id = kStubServerReportId;
  }
  return *next_return_value_++;
}
}  // namespace feedback
