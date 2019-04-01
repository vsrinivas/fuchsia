// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/crashpad_agent/crash_server.h"

#include "third_party/crashpad/util/net/http_transport.h"

namespace fuchsia {
namespace crash {

CrashServer::CrashServer(const std::string& url) : url_(url) {}

bool CrashServer::MakeRequest(const crashpad::HTTPHeaders& headers,
                              std::unique_ptr<crashpad::HTTPBodyStream> stream,
                              std::string* server_report_id) {
  std::unique_ptr<crashpad::HTTPTransport> http_transport(
      crashpad::HTTPTransport::Create());
  for (const auto& header : headers) {
    http_transport->SetHeader(header.first, header.second);
  }
  http_transport->SetBodyStream(std::move(stream));
  http_transport->SetTimeout(60.0);  // 1 minute.
  http_transport->SetURL(url_);
  return http_transport->ExecuteSynchronously(server_report_id);
}

}  // namespace crash
}  // namespace fuchsia
