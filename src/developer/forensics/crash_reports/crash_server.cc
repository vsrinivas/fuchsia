// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/crash_server.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "third_party/crashpad/util/net/http_body.h"
#include "third_party/crashpad/util/net/http_headers.h"
#include "third_party/crashpad/util/net/http_multipart_builder.h"
#include "third_party/crashpad/util/net/http_transport.h"
#include "third_party/crashpad/util/net/url.h"

namespace forensics {
namespace crash_reports {
namespace {

// Wrapper around SizedData that allows crashpad::HTTPMultipartBuilder to upload
// attachments.
class SizedDataReader : public crashpad::FileReaderInterface {
 public:
  SizedDataReader(const SizedData& data) : data_(data) {}

  // crashpad::FileReaderInterface
  crashpad::FileOperationResult Read(void* data, size_t size) override;

  // crashpad::FileSeekerInterface
  crashpad::FileOffset Seek(crashpad::FileOffset offset, int whence) override;

 private:
  const SizedData& data_;
  size_t offset_{};
};

crashpad::FileOperationResult SizedDataReader::Read(void* data, size_t size) {
  if (offset_ >= data_.size()) {
    return 0;
  }

  // Can't read beyond the end of the buffer.
  const auto read_size = std::min(size, data_.size() - offset_);
  memcpy(data, const_cast<uint8_t*>(data_.data()), read_size);
  Seek(read_size, SEEK_CUR);

  return read_size;
}

crashpad::FileOffset SizedDataReader::Seek(crashpad::FileOffset offset, int whence) {
  if (whence == SEEK_SET) {
    offset_ = offset;
  } else if (whence == SEEK_CUR) {
    offset_ += offset;
  } else if (whence == SEEK_END) {
    offset_ = data_.size() + offset;
  } else {
    return -1;
  }

  return offset_;
}

}  // namespace

CrashServer::CrashServer(const std::string& url) : url_(url) {}

bool CrashServer::MakeRequest(const Report& report, std::string* server_report_id) {
  std::vector<SizedDataReader> attachment_readers;
  attachment_readers.reserve(report.Attachments().size() + 1);

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
  return http_transport->ExecuteSynchronously(server_report_id);
}

}  // namespace crash_reports
}  // namespace forensics
