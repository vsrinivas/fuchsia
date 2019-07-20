// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/tests/stub_crash_server.h"

namespace fuchsia {
namespace crash {

const char kStubCrashServerUrl[] = "localhost:1234";

const char kStubServerReportId[] = "server-report-id";

bool StubCrashServer::MakeRequest(const crashpad::HTTPHeaders& headers,
                                  std::unique_ptr<crashpad::HTTPBodyStream> stream,
                                  std::string* server_report_id) {
  *server_report_id = kStubServerReportId;
  return request_return_value_;
}

}  // namespace crash
}  // namespace fuchsia
