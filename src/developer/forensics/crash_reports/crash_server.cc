// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/crash_server.h"

#include <fuchsia/net/http/cpp/fidl.h>
#include <lib/fostr/fidl/fuchsia/net/http/formatting.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <memory>

#include "src/developer/forensics/crash_reports/sized_data_reader.h"
#include "src/developer/forensics/utils/sized_data.h"
#include "src/lib/fsl/socket/blocking_drain.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/vector.h"
#include "third_party/crashpad/util/net/http_body.h"
#include "third_party/crashpad/util/net/http_headers.h"
#include "third_party/crashpad/util/net/http_multipart_builder.h"
#include "third_party/crashpad/util/net/http_transport.h"
#include "third_party/crashpad/util/net/url.h"

namespace forensics {
namespace crash_reports {
namespace {

// Handles executing a HTTP request with fuchsia.net.http.Loader. crashpad::HTTPTransport is used as
// the base class so standard HTTP request building functionality doesn't need to be reimplemented.
//
// |fuchsia.net.http.Loader| is expected to be in |services|.
class HTTPTransportService : public crashpad::HTTPTransport {
 public:
  HTTPTransportService(std::shared_ptr<sys::ServiceDirectory> services, const char* tags)
      : services_(std::move(services)), tags_(tags) {}
  ~HTTPTransportService() override = default;

  CrashServer::UploadStatus Execute(std::string* response_body);

 private:
  bool ExecuteSynchronously(std::string* response_body) override {
    FX_LOGS(FATAL) << "Not implemented";
    return false;
  }

  std::shared_ptr<sys::ServiceDirectory> services_;
  const char* tags_;
};

CrashServer::UploadStatus HTTPTransportService::Execute(std::string* response_body) {
  using namespace fuchsia::net::http;

  // Create the headers for the request.
  std::vector<Header> http_headers;
  for (const auto& [name, value] : headers()) {
    http_headers.push_back(Header{
        .name = std::vector<uint8_t>(name.begin(), name.end()),
        .value = std::vector<uint8_t>(value.begin(), value.end()),
    });
  }

  // Create the request body as a single VMO.
  // TODO(fxbug.dev/59191): Consider using a zx::socket to transmit the HTTP request body to the
  // server piecewise.
  std::vector<uint8_t> body;

  // Reserve 256 kb for the request body.
  body.reserve(256 * 1024);
  while (true) {
    // Copy the body in 32 kb chunks.
    std::array<uint8_t, 32 * 1024> buf;
    const auto result = body_stream()->GetBytesBuffer(buf.data(), buf.max_size());

    FX_CHECK(result >= 0);
    if (result == 0) {
      break;
    }

    body.insert(body.end(), buf.data(), buf.data() + result);
  }

  fsl::SizedVmo body_vmo;
  if (!fsl::VmoFromVector(body, &body_vmo)) {
    FX_LOGST(ERROR, tags_) << "Failed to create VMO";
    return CrashServer::UploadStatus::kFailure;
  }

  // Create the request.
  Request request;
  request.set_method(method())
      .set_url(url())
      .set_deadline(zx::deadline_after(zx::sec((uint64_t)timeout())).get())
      .set_headers(std::move(http_headers))
      .set_body(Body::WithBuffer(std::move(body_vmo).ToTransport()));

  // Connect to the Loader service.
  LoaderSyncPtr loader;
  FX_CHECK(services_->Connect(loader.NewRequest()) == ZX_OK);

  // Execute the request.
  Response response;
  if (const auto status = loader->Fetch(std::move(request), &response); status != ZX_OK) {
    FX_PLOGST(WARNING, tags_, status) << "Lost connection with fuchsia.net.http.Loader";
    return CrashServer::UploadStatus::kFailure;
  }

  if (response.has_error()) {
    FX_LOGST(WARNING, tags_) << "Experienced network error: " << response.error();
    return CrashServer::UploadStatus::kFailure;
  }

  if (!response.has_status_code()) {
    FX_LOGST(ERROR, tags_) << "No status code received";
    return CrashServer::UploadStatus::kFailure;
  }

  if (response.status_code() == 429) {
    FX_LOGST(WARNING, tags_) << "Upload throttled by server";
    return CrashServer::UploadStatus::kThrottled;
  }

  if (response.status_code() < 200 || response.status_code() >= 204) {
    FX_LOGST(WARNING, tags_) << "Failed to upload report, received HTTP status code "
                             << response.status_code();
    return CrashServer::UploadStatus::kFailure;
  }

  // Read the response into |response_body|.
  if (!response.has_body()) {
    FX_LOGST(WARNING, tags_) << "Http response is missing body";
    return CrashServer::UploadStatus::kFailure;
  }

  response_body->clear();
  if (!fsl::BlockingDrainFrom(std::move(*response.mutable_body()),
                              [&response_body](const void* data, uint32_t len) {
                                const char* begin = static_cast<const char*>(data);
                                response_body->insert(response_body->end(), begin, begin + len);
                                return len;
                              })) {
    FX_LOGST(WARNING, tags_) << "Failed to read http body";
    return CrashServer::UploadStatus::kFailure;
  }

  return CrashServer::UploadStatus::kSuccess;
}

}  // namespace

CrashServer::CrashServer(std::shared_ptr<sys::ServiceDirectory> services, const std::string& url,
                         SnapshotManager* snapshot_manager, LogTags* tags)
    : services_(services), url_(url), snapshot_manager_(snapshot_manager), tags_(tags) {}

CrashServer::UploadStatus CrashServer::MakeRequest(const Report& report,
                                                   std::string* server_report_id) {
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

  auto http_transport = std::make_unique<HTTPTransportService>(services_, tags_->Get(report.Id()));

  for (const auto& header : headers) {
    http_transport->SetHeader(header.first, header.second);
  }
  http_transport->SetBodyStream(http_multipart_builder.GetBodyStream());
  http_transport->SetTimeout(60.0);  // 1 minute.
  http_transport->SetURL(url_);

  return http_transport->Execute(server_report_id);
}

}  // namespace crash_reports
}  // namespace forensics
