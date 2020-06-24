// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/crash_server.h"

#include "third_party/crashpad/util/net/http_body.h"
#include "third_party/crashpad/util/net/http_headers.h"
#include "third_party/crashpad/util/net/http_multipart_builder.h"
#include "third_party/crashpad/util/net/http_transport.h"
#include "third_party/crashpad/util/net/url.h"

namespace forensics {
namespace crash_reports {

CrashServer::CrashServer(const std::string& url) : url_(url) {}

bool CrashServer::MakeRequest(const std::map<std::string, std::string>& annotations,
                              const std::map<std::string, crashpad::FileReader*>& attachments,
                              std::string* server_report_id) {
  // We have to build the MIME multipart message ourselves as all the public Crashpad helpers are
  // asynchronous and we won't be able to know the upload status nor the server report ID.
  crashpad::HTTPMultipartBuilder http_multipart_builder;
  http_multipart_builder.SetGzipEnabled(true);
  for (const auto& [key, value] : annotations) {
    http_multipart_builder.SetFormData(key, value);
  }
  for (const auto& [filename, content] : attachments) {
    http_multipart_builder.SetFileAttachment(filename, filename, content,
                                             "application/octet-stream");
  }
  crashpad::HTTPHeaders headers;
  http_multipart_builder.PopulateContentHeaders(&headers);

  std::unique_ptr<crashpad::HTTPTransport> http_transport(crashpad::HTTPTransport::Create());
  for (const auto& header : headers) {
    http_transport->SetHeader(header.first, header.second);
  }
  http_transport->SetBodyStream(http_multipart_builder.GetBodyStream());
  http_transport->SetTimeout(60.0);  // 1 minute.
  http_transport->SetURL(url_);
  return http_transport->ExecuteSynchronously(server_report_id);
}

}  // namespace crash_reports
}  // namespace forensics
