// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/tests/stub_crash_server.h"

#include "src/developer/feedback/crashpad_agent/crash_report_util.h"

namespace feedback {

const char kStubCrashServerUrl[] = "localhost:1234";

const char kStubServerReportId[] = "server-report-id";

bool StubCrashServer::MakeRequest(const std::map<std::string, std::string>& annotations,
                                  const std::map<std::string, crashpad::FileReader*>& attachments,
                                  std::string* server_report_id) {
  annotations_ = annotations;
  *server_report_id = kStubServerReportId;
  return request_return_value_;
}

}  // namespace feedback
