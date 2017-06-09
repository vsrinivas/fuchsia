// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/gcs/cloud_storage_impl.h"

#include <fcntl.h>

#include <string>

#include "apps/ledger/src/glue/socket/socket_pair.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/ftl/files/eintr_wrapper.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/file_descriptor.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/ascii.h"
#include "lib/ftl/strings/concatenate.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/ftl/strings/string_view.h"
#include "lib/mtl/socket/files.h"
#include "lib/mtl/vmo/file.h"

namespace gcs {

namespace {

const char kAuthorizationHeader[] = "authorization";
const char kContentLengthHeader[] = "content-length";

constexpr ftl::StringView kApiEndpoint =
    "https://firebasestorage.googleapis.com/v0/b/";
constexpr ftl::StringView kBucketNameSuffix = ".appspot.com";

network::HttpHeaderPtr GetHeader(
    const fidl::Array<network::HttpHeaderPtr>& headers,
    const std::string& header_name) {
  for (const auto& header : headers.storage()) {
    if (ftl::EqualsCaseInsensitiveASCII(header->name.get(), header_name)) {
      return header.Clone();
    }
  }
  return nullptr;
}

network::HttpHeaderPtr MakeAuthorizationHeader(const std::string& auth_token) {
  network::HttpHeaderPtr authorization_header = network::HttpHeader::New();
  authorization_header->name = kAuthorizationHeader;
  authorization_header->value = "Bearer " + auth_token;
  return authorization_header;
}

void RunUploadObjectCallback(const std::function<void(Status)>& callback,
                             Status status,
                             network::URLResponsePtr response) {
  // A precondition failure means the object already exist.
  if (response->status_code == 412) {
    callback(Status::OBJECT_ALREADY_EXISTS);
    return;
  }
  callback(status);
}

std::string GetUrlPrefix(const std::string& firebase_id,
                         const std::string& cloud_prefix) {
  return ftl::Concatenate(
      {kApiEndpoint, firebase_id, kBucketNameSuffix, "/o/", cloud_prefix});
}

}  // namespace

CloudStorageImpl::CloudStorageImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                                   ledger::NetworkService* network_service,
                                   const std::string& firebase_id,
                                   const std::string& cloud_prefix)
    : task_runner_(std::move(task_runner)),
      network_service_(network_service),
      url_prefix_(GetUrlPrefix(firebase_id, cloud_prefix)) {}

CloudStorageImpl::~CloudStorageImpl() {}

void CloudStorageImpl::UploadObject(
    std::string auth_token,
    const std::string& key,
    mx::vmo data,
    const std::function<void(Status)>& callback) {
  std::string url = GetUploadUrl(key);

  uint64_t data_size;
  mx_status_t status = data.get_size(&data_size);
  if (status != MX_OK) {
    FTL_LOG(ERROR) << "Failed to retrieve the size of the vmo.";
    callback(Status::INTERNAL_ERROR);
    return;
  }

  auto request_factory = ftl::MakeCopyable([
    auth_token = std::move(auth_token), url = std::move(url),
    task_runner = task_runner_, data = std::move(data), data_size
  ] {
    network::URLRequestPtr request(network::URLRequest::New());
    request->url = url;
    request->method = "POST";
    request->auto_follow_redirects = true;

    // Authorization header.
    if (!auth_token.empty()) {
      request->headers.push_back(MakeAuthorizationHeader(auth_token));
    }

    // Content-Length header.
    network::HttpHeaderPtr content_length_header = network::HttpHeader::New();
    content_length_header->name = kContentLengthHeader;
    content_length_header->value = ftl::NumberToString(data_size);
    request->headers.push_back(std::move(content_length_header));

    // x-goog-if-generation-match header. This ensures that files are never
    // overwritten.
    network::HttpHeaderPtr generation_match_header = network::HttpHeader::New();
    generation_match_header->name = "x-goog-if-generation-match";
    generation_match_header->value = "0";
    request->headers.push_back(std::move(generation_match_header));

    mx::vmo duplicated_data;
    data.duplicate(MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ,
                   &duplicated_data);
    request->body = network::URLBody::New();
    request->body->set_buffer(std::move(duplicated_data));
    return request;
  });

  Request(std::move(request_factory),
          [callback](Status status, network::URLResponsePtr response) {
            RunUploadObjectCallback(std::move(callback), status,
                                    std::move(response));
          });
}

void CloudStorageImpl::DownloadObject(
    std::string auth_token,
    const std::string& key,
    const std::function<void(Status status, uint64_t size, mx::socket data)>&
        callback) {
  std::string url = GetDownloadUrl(key);

  Request([ auth_token = std::move(auth_token), url = std::move(url) ] {
    network::URLRequestPtr request(network::URLRequest::New());
    request->url = url;
    request->method = "GET";
    request->auto_follow_redirects = true;
    if (!auth_token.empty()) {
      request->headers.push_back(MakeAuthorizationHeader(auth_token));
    }
    return request;
  },
          [ this, callback = std::move(callback) ](
              Status status, network::URLResponsePtr response) {
            OnDownloadResponseReceived(std::move(callback), status,
                                       std::move(response));
          });
}

std::string CloudStorageImpl::GetDownloadUrl(ftl::StringView key) {
  FTL_DCHECK(key.find('/') == std::string::npos);
  return ftl::Concatenate({url_prefix_, key, "?alt=media"});
}

std::string CloudStorageImpl::GetUploadUrl(ftl::StringView key) {
  FTL_DCHECK(key.find('/') == std::string::npos);
  return ftl::Concatenate({url_prefix_, key});
}

void CloudStorageImpl::Request(
    std::function<network::URLRequestPtr()> request_factory,
    const std::function<void(Status status, network::URLResponsePtr response)>&
        callback) {
  network_service_->Request(std::move(request_factory),
                            [this, callback](network::URLResponsePtr response) {
                              OnResponse(std::move(callback),
                                         std::move(response));
                            });
}

void CloudStorageImpl::OnResponse(
    const std::function<void(Status status, network::URLResponsePtr response)>&
        callback,
    network::URLResponsePtr response) {
  if (response->error) {
    FTL_LOG(ERROR) << response->url << " error "
                   << response->error->description;
    callback(Status::NETWORK_ERROR, std::move(response));
    return;
  }

  if (response->status_code == 404) {
    callback(Status::NOT_FOUND, std::move(response));
    return;
  }

  if (response->status_code != 200 && response->status_code != 204) {
    FTL_LOG(ERROR) << response->url << " error " << response->status_line;
    callback(Status::SERVER_ERROR, std::move(response));
    return;
  }

  callback(Status::OK, std::move(response));
}

void CloudStorageImpl::OnDownloadResponseReceived(
    const std::function<void(Status status, uint64_t size, mx::socket data)>
        callback,
    Status status,
    network::URLResponsePtr response) {
  if (status != Status::OK) {
    callback(status, 0u, mx::socket());
    return;
  }

  network::HttpHeaderPtr size_header =
      GetHeader(response->headers, kContentLengthHeader);
  if (!size_header) {
    callback(Status::PARSE_ERROR, 0u, mx::socket());
    return;
  }

  uint64_t expected_file_size;
  if (!ftl::StringToNumberWithError(size_header->value.get(),
                                    &expected_file_size)) {
    callback(Status::PARSE_ERROR, 0u, mx::socket());
    return;
  }

  network::URLBodyPtr body = std::move(response->body);
  FTL_DCHECK(body->is_stream());
  callback(Status::OK, expected_file_size, std::move(body->get_stream()));
}

}  // namespace gcs
