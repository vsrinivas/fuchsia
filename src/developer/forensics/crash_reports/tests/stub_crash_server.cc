// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/tests/stub_crash_server.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

#include "lib/fit/function.h"
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

bool StubCrashServer::HasPendingRequest() const { return has_pending_request_; }

void StubCrashServer::MakeRequest(const Report& report, const Snapshot& snapshot,
                                  ::fit::function<void(UploadStatus, std::string)> callback) {
  latest_annotations_ = report.Annotations();
  latest_attachment_keys_.clear();
  for (const auto& [key, _] : report.Attachments()) {
    latest_attachment_keys_.push_back(key);
  }

  if (report.Minidump().has_value()) {
    latest_attachment_keys_.push_back("uploadFileMinidump");
  }

  if (std::holds_alternative<ManagedSnapshot>(snapshot)) {
    const auto& s = std::get<ManagedSnapshot>(snapshot);

    if (auto archive = s.LockArchive(); archive) {
      latest_attachment_keys_.push_back(archive->key);
    }
  } else {
    const auto& s = std::get<MissingSnapshot>(snapshot);
    for (const auto& [key, value] : s.PresenceAnnotations()) {
      latest_annotations_.Set(key, value);
    }
  }

  FX_CHECK(ExpectRequest()) << fxl::StringPrintf(
      "no more calls to MakeRequest() expected (%lu/%lu calls made)",
      std::distance(request_return_values_.cbegin(), next_return_value_),
      request_return_values_.size());

  async::PostDelayedTask(
      dispatcher_,
      [this, callback = std::move(callback), status = *next_return_value_]() mutable {
        const std::string server_report_id = (status) ? kStubServerReportId : "";
        has_pending_request_ = false;
        callback(status, server_report_id);
      },
      response_delay_);

  has_pending_request_ = true;
  ++next_return_value_;
}

}  // namespace crash_reports
}  // namespace forensics
