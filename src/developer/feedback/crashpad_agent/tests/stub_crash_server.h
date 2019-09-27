// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_TESTS_STUB_CRASH_SERVER_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_TESTS_STUB_CRASH_SERVER_H_

#include <map>
#include <string>

#include "src/developer/feedback/crashpad_agent/crash_server.h"

namespace feedback {

extern const char kStubCrashServerUrl[];
extern const char kStubServerReportId[];

class StubCrashServer : public CrashServer {
 public:
  StubCrashServer(bool request_return_value)
      : CrashServer(kStubCrashServerUrl), request_return_value_(request_return_value) {}

  bool MakeRequest(const std::map<std::string, std::string>& annotations,
                   const std::map<std::string, crashpad::FileReader*>& attachments,
                   std::string* server_report_id) override;

  const std::map<std::string, std::string>& annotations() { return annotations_; }

 private:
  const bool request_return_value_;

  std::string stream_;
  std::map<std::string, std::string> annotations_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_TESTS_STUB_CRASH_SERVER_H_
