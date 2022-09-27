// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/crash_server.h"

#include <fuchsia/net/http/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <memory>
#include <variant>

#include <src/lib/fostr/fidl/fuchsia/net/http/formatting.h>

#include "src/developer/forensics/crash_reports/sized_data_reader.h"
#include "src/developer/forensics/utils/sized_data.h"
#include "src/lib/fsl/socket/blocking_drain.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/vector.h"
#include "src/lib/fxl/strings/substitute.h"
#include "third_party/crashpad/util/net/http_body.h"
#include "third_party/crashpad/util/net/http_headers.h"
#include "third_party/crashpad/util/net/http_multipart_builder.h"
#include "third_party/crashpad/util/net/http_transport.h"
#include "third_party/crashpad/util/net/url.h"

namespace forensics {
namespace crash_reports {
namespace {

// Builds a fuchsia::net::http::Request. crashpad::HTTPTransport is used as the base class so
// standard HTTP request building functionality doesn't need to be reimplemented.
class HttpRequestBuilder : public crashpad::HTTPTransport {
 public:
  std::optional<fuchsia::net::http::Request> Build() && {
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
      return std::nullopt;
    }

    // Create the request.
    Request request;
    request.set_method(method())
        .set_url(url())
        .set_deadline(zx::deadline_after(zx::sec((uint64_t)timeout())).get())
        .set_headers(std::move(http_headers))
        .set_body(Body::WithBuffer(std::move(body_vmo).ToTransport()));

    return request;
  }

 private:
  bool ExecuteSynchronously(std::string* response_body) override {
    FX_LOGS(FATAL) << "Not implemented";
    return false;
  }
};

}  // namespace

CrashServer::CrashServer(async_dispatcher_t* dispatcher,
                         std::shared_ptr<sys::ServiceDirectory> services, const std::string& url,
                         LogTags* tags)
    : dispatcher_(dispatcher), services_(services), url_(url), tags_(tags) {
  services_->Connect(loader_.NewRequest(dispatcher_));
  loader_.set_error_handler([](const zx_status_t status) {
    FX_PLOGS(WARNING, status) << "Lost connection to fuchsia.net.http.Loader";
  });
}

void CrashServer::MakeRequest(const Report& report, const Snapshot& snapshot,
                              ::fit::function<void(UploadStatus, std::string)> callback) {
  // Make sure a call to fuchsia.net.http.Loader/Fetch isn't outstanding.
  FX_CHECK(!pending_request_);

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

  // Append the product and version parameters to the URL.
  const std::map<std::string, std::string> annotations = PrepareAnnotations(report, snapshot);
  FX_CHECK(annotations.count("product") != 0);
  FX_CHECK(annotations.count("version") != 0);
  const std::string url = fxl::Substitute("$0?product=$1&version=$2", url_,
                                          crashpad::URLEncode(annotations.at("product")),
                                          crashpad::URLEncode(annotations.at("version")));

  // We have to build the MIME multipart message ourselves as all the public Crashpad helpers are
  // asynchronous and we won't be able to know the upload status nor the server report ID.
  crashpad::HTTPMultipartBuilder http_multipart_builder;
  http_multipart_builder.SetGzipEnabled(true);

  for (const auto& [key, value] : annotations) {
    http_multipart_builder.SetFormData(key, value);
  }

  // Add the snapshot archive (only relevant for ManagedSnapshots).
  if (std::holds_alternative<ManagedSnapshot>(snapshot)) {
    const auto& s = std::get<ManagedSnapshot>(snapshot);
    if (const auto archive = s.LockArchive(); archive) {
      attachment_readers.emplace_back(archive->value);
      file_readers.emplace(archive->key, &attachment_readers.back());
    }
  }

  for (const auto& [filename, content] : file_readers) {
    http_multipart_builder.SetFileAttachment(filename, filename, content,
                                             "application/octet-stream");
  }
  crashpad::HTTPHeaders headers;
  http_multipart_builder.PopulateContentHeaders(&headers);

  HttpRequestBuilder request_builder;
  for (const auto& header : headers) {
    request_builder.SetHeader(header.first, header.second);
  }
  request_builder.SetBodyStream(http_multipart_builder.GetBodyStream());
  request_builder.SetTimeout(60.0);  // 1 minute.
  request_builder.SetURL(url);

  auto request = std::move(request_builder).Build();
  if (!request.has_value()) {
    callback(CrashServer::UploadStatus::kFailure, "");
    return;
  }

  if (!loader_) {
    services_->Connect(loader_.NewRequest(dispatcher_));
  }

  const std::string tags = tags_->Get(report.Id());
  loader_->Fetch(std::move(request.value()), [this, tags, callback = std::move(callback)](
                                                 fuchsia::net::http::Response response) mutable {
    pending_request_ = false;

    if (response.has_error()) {
      FX_LOGST(WARNING, tags.c_str()) << "Experienced network error: " << response.error();
      if (response.error() == fuchsia::net::http::Error::DEADLINE_EXCEEDED) {
        callback(CrashServer::UploadStatus::kTimedOut, "");
      } else {
        callback(CrashServer::UploadStatus::kFailure, "");
      }
      return;
    }

    std::string response_body;
    if (response.has_body()) {
      if (!fsl::BlockingDrainFrom(std::move(*response.mutable_body()),
                                  [&response_body](const void* data, uint32_t len) {
                                    const char* begin = static_cast<const char*>(data);
                                    response_body.insert(response_body.end(), begin, begin + len);
                                    return len;
                                  })) {
        FX_LOGST(WARNING, tags.c_str()) << "Failed to read http body";
        response_body.clear();
      }
    } else {
      FX_LOGST(WARNING, tags.c_str()) << "Http response is missing body";
    }

    if (!response.has_status_code()) {
      FX_LOGST(ERROR, tags.c_str()) << "No status code received: " << response_body;
      callback(CrashServer::UploadStatus::kFailure, "");
      return;
    }

    if (response.status_code() == 429) {
      FX_LOGST(WARNING, tags.c_str()) << "Upload throttled by server: " << response_body;
      callback(CrashServer::UploadStatus::kThrottled, "");
      return;
    }

    if (response.status_code() < 200 || response.status_code() >= 204) {
      FX_LOGST(WARNING, tags.c_str()) << "Failed to upload report, received HTTP status code "
                                      << response.status_code() << ": " << response_body;
      callback(CrashServer::UploadStatus::kFailure, "");
      return;
    }

    if (response_body.empty()) {
      callback(CrashServer::UploadStatus::kFailure, "");
    } else {
      callback(CrashServer::UploadStatus::kSuccess, std::move(response_body));
    }
  });

  pending_request_ = true;
}

std::map<std::string, std::string> CrashServer::PrepareAnnotations(const Report& report,
                                                                   const Snapshot& snapshot) {
  // Start with annotations from |report| and only add "presence" annotations.
  //
  // If |snapshot| is a MissingSnapshot, they contain potentially new information about why the
  // underlying data was dropped by the SnapshotManager.
  auto annotations = report.Annotations();

  if (std::holds_alternative<MissingSnapshot>(snapshot)) {
    const auto& s = std::get<MissingSnapshot>(snapshot);
    annotations.Set(s.PresenceAnnotations());
  }

  return annotations.Raw();
}

}  // namespace crash_reports
}  // namespace forensics
