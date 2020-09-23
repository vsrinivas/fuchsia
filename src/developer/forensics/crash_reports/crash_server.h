// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CRASH_SERVER_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CRASH_SERVER_H_

#include <fuchsia/mem/cpp/fidl.h>

#include <map>
#include <string>

#include "src/developer/forensics/crash_reports/report.h"
#include "src/developer/forensics/crash_reports/snapshot_manager.h"
#include "src/lib/fxl/macros.h"
#include "third_party/crashpad/util/file/file_reader.h"
#include "third_party/crashpad/util/net/http_body.h"
#include "third_party/crashpad/util/net/http_headers.h"
#include "third_party/crashpad/util/net/http_multipart_builder.h"
#include "third_party/crashpad/util/net/http_transport.h"
#include "third_party/crashpad/util/net/url.h"

namespace forensics {
namespace crash_reports {

// Represents the HTTP crash server to which the agent uploads crash reports to.
class CrashServer {
 public:
  CrashServer(const std::string& url, SnapshotManager* snapshot_manager);

  virtual ~CrashServer() {}

  // Makes the HTTP request using |report|.
  //
  // Returns whether the request was successful, defined as returning a HTTP status code in the
  // range [200-203]. In case of success, |server_report_id| is set to the crash report id on the
  // server.
  virtual bool MakeRequest(const Report& report, std::string* server_report_id);

 private:
  const std::string url_;
  SnapshotManager* snapshot_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CrashServer);
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CRASH_SERVER_H_
