// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_TESTS_STUB_CRASH_SERVER_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_TESTS_STUB_CRASH_SERVER_H_

#include <map>
#include <string>
#include <vector>

#include "src/developer/forensics/crash_reports/crash_server.h"

namespace forensics {
namespace crash_reports {

extern const char kStubCrashServerUrl[];
extern const char kStubServerReportId[];

class StubCrashServer : public CrashServer {
 public:
  StubCrashServer(const std::vector<bool>& request_return_values)
      : CrashServer(kStubCrashServerUrl, nullptr),
        request_return_values_(request_return_values),
        snapshot_manager_() {
    next_return_value_ = request_return_values_.cbegin();
  }

  ~StubCrashServer();

  void AddSnapshotManager(SnapshotManager* snapshot_manager);

  bool MakeRequest(const Report& report, std::string* server_report_id) override;

  // Whether the crash server expects at least one more call to MakeRequest().
  bool ExpectRequest() { return next_return_value_ != request_return_values_.cend(); }

  // Returns the annotations that were passed to the latest MakeRequest() call.
  const std::map<std::string, std::string>& latest_annotations() { return latest_annotations_; }

  // Returns the keys for the attachments that were passed to the latest MakeRequest() call.
  const std::vector<std::string>& latest_attachment_keys() { return latest_attachment_keys_; }

 private:
  const std::vector<bool> request_return_values_;
  std::vector<bool>::const_iterator next_return_value_;

  SnapshotManager* snapshot_manager_;

  std::map<std::string, std::string> latest_annotations_;
  std::vector<std::string> latest_attachment_keys_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_TESTS_STUB_CRASH_SERVER_H_
