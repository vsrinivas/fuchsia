// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_CRASHPAD_AGENT_CRASH_SERVER_H_
#define SRC_DEVELOPER_CRASHPAD_AGENT_CRASH_SERVER_H_

#include <string>

#include "src/lib/fxl/macros.h"
#include "third_party/crashpad/util/net/http_body.h"
#include "third_party/crashpad/util/net/http_headers.h"

namespace fuchsia {
namespace crash {

// Represents the HTTP crash server to which the agent uploads crash reports to.
class CrashServer {
 public:
  explicit CrashServer(const std::string& url);
  virtual ~CrashServer() {}

  // Makes the HTTP request using the |headers| and the |stream| to generate the
  // HTTP body.
  //
  // Returns whether the request was successful, defined as returning a HTTP
  // status code in the range [200-203]. In case of success, |server_report_id|
  // is set to the crash report id on the server.
  virtual bool MakeRequest(const crashpad::HTTPHeaders& headers,
                           std::unique_ptr<crashpad::HTTPBodyStream> stream,
                           std::string* server_report_id);

 private:
  const std::string url_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CrashServer);
};

}  // namespace crash
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_CRASHPAD_AGENT_CRASH_SERVER_H_
