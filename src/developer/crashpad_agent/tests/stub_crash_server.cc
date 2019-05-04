// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/crashpad_agent/tests/stub_crash_server.h"

namespace fuchsia {
namespace crash {

const char kStubCrashServerUrl[] = "localhost:1234";

bool StubCrashServer::MakeRequest(
    const crashpad::HTTPHeaders& headers,
    std::unique_ptr<crashpad::HTTPBodyStream> stream,
    std::string* server_report_id) {
  // TODO(frousseau): check this is the one written in the local Crashpad
  // database.
  *server_report_id = "untestedReportdId";
  return request_return_value_;
}

}  // namespace crash
}  // namespace fuchsia
