// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/crash_server.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "src/developer/forensics/crash_reports/sized_data_reader.h"
#include "src/developer/forensics/utils/sized_data.h"
#include "third_party/crashpad/util/net/http_body.h"
#include "third_party/crashpad/util/net/http_headers.h"
#include "third_party/crashpad/util/net/http_multipart_builder.h"
#include "third_party/crashpad/util/net/http_transport.h"
#include "third_party/crashpad/util/net/url.h"

namespace forensics {
namespace crash_reports {

CrashServer::CrashServer(const std::string& url, SnapshotManager* snapshot_manager)
    : url_(url), snapshot_manager_(snapshot_manager) {}

bool CrashServer::MakeRequest(const Report& report, std::string* server_report_id) {
  std::vector<SizedDataReader> attachment_readers;
  attachment_readers.reserve(report.Attachments().size() + 2u /*minidump and snapshot*/);

  std::map<std::string, crashpad::FileReaderInterface*> file_readers;

  for (const auto& [k, v] : report.Attachments()) {
    if (k.empty()) {
      continue;
    }
    attachment_readers.emplace_back(v);
    file_readers.emplace(k, &attachment_readers.back());
  }

  if (report.Minidump().has_value()) {
    attachment_readers.emplace_back(report.Minidump().value());
    file_readers.emplace("uploadFileMinidump", &attachment_readers.back());
  }

  // We have to build the MIME multipart message ourselves as all the public Crashpad helpers are
  // asynchronous and we won't be able to know the upload status nor the server report ID.
  crashpad::HTTPMultipartBuilder http_multipart_builder;
  http_multipart_builder.SetGzipEnabled(true);
  for (const auto& [key, value] : report.Annotations()) {
    http_multipart_builder.SetFormData(key, value);
  }

  // Add the snapshot archive and annotations.
  auto snapshot = snapshot_manager_->GetSnapshot(report.SnapshotUuid());
  if (const auto archive = snapshot.LockArchive(); archive) {
    attachment_readers.emplace_back(archive->value);
    file_readers.emplace(archive->key, &attachment_readers.back());
  }

  if (const auto annotations = snapshot.LockAnnotations(); annotations) {
    for (const auto& [key, value] : *annotations) {
      http_multipart_builder.SetFormData(key, value);
    }
  }

  for (const auto& [filename, content] : file_readers) {
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

  // If the upload is successful, let |snapshot_manager_| know the snapshot isn't needed for this
  // report any more.
  if (http_transport->ExecuteSynchronously(server_report_id)) {
    snapshot_manager_->Release(report.SnapshotUuid());
    return true;
  }

  return false;
}

}  // namespace crash_reports
}  // namespace forensics
