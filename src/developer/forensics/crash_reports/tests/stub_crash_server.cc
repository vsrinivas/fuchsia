// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/tests/stub_crash_server.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace crash_reports {

const char kStubCrashServerUrl[] = "localhost:1234";

const char kStubServerReportId[] = "server-report-id";

StubCrashServer::~StubCrashServer() {
  FX_CHECK(!ExpectRequest()) << fxl::StringPrintf(
      "expected %ld more calls to MakeRequest() (%ld/%lu calls made)",
      std::distance(next_return_value_, request_return_values_.cend()),
      std::distance(request_return_values_.cbegin(), next_return_value_),
      request_return_values_.size());
}

void StubCrashServer::AddSnapshotManager(SnapshotManager* snapshot_manager) {
  FX_CHECK(snapshot_manager);
  snapshot_manager_ = snapshot_manager;
}

bool StubCrashServer::MakeRequest(const Report& report, std::string* server_report_id) {
  latest_annotations_ = report.Annotations();
  latest_attachment_keys_.clear();
  for (const auto& [key, _] : report.Attachments()) {
    latest_attachment_keys_.push_back(key);
  }

  if (report.Minidump().has_value()) {
    latest_attachment_keys_.push_back("uploadFileMinidump");
  }

  if (snapshot_manager_) {
    auto snapshot = snapshot_manager_->GetSnapshot(report.SnapshotUuid());
    if (auto annotations = snapshot.LockAnnotations(); annotations) {
      for (const auto& [key, value] : *annotations) {
        latest_annotations_.emplace(key, value);
      }
    }

    if (auto archive = snapshot.LockArchive(); archive) {
      latest_attachment_keys_.push_back(archive->key);
    }
  }

  FX_CHECK(ExpectRequest()) << fxl::StringPrintf(
      "no more calls to MakeRequest() expected (%lu/%lu calls made)",
      std::distance(request_return_values_.cbegin(), next_return_value_),
      request_return_values_.size());
  if (*next_return_value_) {
    *server_report_id = kStubServerReportId;
  }
  return *next_return_value_++;
}

}  // namespace crash_reports
}  // namespace forensics
