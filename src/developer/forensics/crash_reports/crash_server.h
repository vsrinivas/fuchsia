// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CRASH_SERVER_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CRASH_SERVER_H_

#include <map>
#include <string>

#include "src/lib/fxl/macros.h"
#include "third_party/crashpad/util/file/file_reader.h"

namespace forensics {
namespace crash_reports {

// Represents the HTTP crash server to which the agent uploads crash reports to.
class CrashServer {
 public:
  explicit CrashServer(const std::string& url);
  virtual ~CrashServer() {}

  // Makes the HTTP request using the |annotations| and |attachments|.
  //
  // Returns whether the request was successful, defined as returning a HTTP status code in the
  // range [200-203]. In case of success, |server_report_id| is set to the crash report id on the
  // server.
  virtual bool MakeRequest(const std::map<std::string, std::string>& annotations,
                           const std::map<std::string, crashpad::FileReader*>& attachments,
                           std::string* server_report_id);

 private:
  const std::string url_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CrashServer);
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CRASH_SERVER_H_
