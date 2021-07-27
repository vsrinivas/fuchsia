// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CRASH_SERVER_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CRASH_SERVER_H_

#include <fuchsia/net/http/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>

#include <map>
#include <string>

#include "src/developer/forensics/crash_reports/log_tags.h"
#include "src/developer/forensics/crash_reports/report.h"
#include "src/developer/forensics/crash_reports/snapshot.h"
#include "src/lib/fxl/macros.h"

namespace forensics {
namespace crash_reports {

// Represents the HTTP crash server to which the agent uploads crash reports to.
//
// |fuchsia.net.http.Loader| is expected to be in |services|.
class CrashServer {
 public:
  enum UploadStatus { kSuccess, kFailure, kThrottled, kTimedOut };

  CrashServer(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
              const std::string& url, LogTags* tags);

  virtual ~CrashServer() {}

  virtual bool HasPendingRequest() const { return pending_request_; }

  // Makes the HTTP request using |report| and |snapshot|.
  //
  // Executes |callback| on completion with whether the request was successful (HTTP status
  // code [200-203]) and the crash report id on the server, if the request was successful.
  //
  // Note: Only a single call to MakeRequest can be outstanding at a time.
  virtual void MakeRequest(const Report& report, Snapshot snapshot,
                           ::fit::function<void(UploadStatus, std::string)> callback);

 private:
  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  const std::string url_;
  LogTags* tags_;

  bool pending_request_{false};
  fuchsia::net::http::LoaderPtr loader_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CrashServer);
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CRASH_SERVER_H_
