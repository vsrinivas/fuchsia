// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/gcs/cloud_storage_impl.h"

#include <fcntl.h>

#include <string>

#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fsl/socket/files.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fxl/files/eintr_wrapper.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/file_descriptor.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/ascii.h"
#include "lib/fxl/strings/concatenate.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_view.h"
#include "peridot/bin/ledger/glue/socket/socket_pair.h"

namespace gcs {

namespace {

const char kAuthorizationHeader[] = "authorization";
const char kContentLengthHeader[] = "content-length";

constexpr fxl::StringView kApiEndpoint =
    "https://firebasestorage.googleapis.com/v0/b/";
constexpr fxl::StringView kBucketNameSuffix = ".appspot.com";

network::HttpHeaderPtr GetHeader(
    const fidl::Array<network::HttpHeaderPtr>& headers,
    const std::string& header_name) {
  for (const auto& header : headers.storage()) {
    if (fxl::EqualsCaseInsensitiveASCII(header->name.get(), header_name)) {
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

void RunUploadObjectCallback(std::function<void(Status)> callback,
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
  return fxl::Concatenate(
      {kApiEndpoint, firebase_id, kBucketNameSuffix, "/o/", cloud_prefix});
}

}  // namespace

CloudStorageImpl::CloudStorageImpl(fxl::RefPtr<fxl::TaskRunner> task_runner,
                                   ledger::NetworkService* network_service,
                                   const std::string& firebase_id,
                                   const std::string& cloud_prefix)
    : task_runner_(std::move(task_runner)),
      network_service_(network_service),
      url_prefix_(GetUrlPrefix(firebase_id, cloud_prefix)) {}

CloudStorageImpl::~CloudStorageImpl() {}

void CloudStorageImpl::UploadObject(std::string auth_token,
                                    const std::string& key,
                                    zx::vmo data,
                                    std::function<void(Status)> callback) {
  std::string url = GetUploadUrl(key);

  uint64_t data_size;
  zx_status_t status = data.get_size(&data_size);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to retrieve the size of the vmo.";
    callback(Status::INTERNAL_ERROR);
    return;
  }

  auto request_factory = fxl::MakeCopyable(
      [auth_token = std::move(auth_token), url = std::move(url),
       task_runner = task_runner_, data = std::move(data), data_size] {
        network::URLRequestPtr request(network::URLRequest::New());
        request->url = url;
        request->method = "POST";
        request->auto_follow_redirects = true;

        // Authorization header.
        if (!auth_token.empty()) {
          request->headers.push_back(MakeAuthorizationHeader(auth_token));
        }

        // Content-Length header.
        network::HttpHeaderPtr content_length_header =
            network::HttpHeader::New();
        content_length_header->name = kContentLengthHeader;
        content_length_header->value = fxl::NumberToString(data_size);
        request->headers.push_back(std::move(content_length_header));

        zx::vmo duplicated_data;
        data.duplicate(ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ,
                       &duplicated_data);
        request->body = network::URLBody::New();
        request->body->set_buffer(std::move(duplicated_data));
        return request;
      });

  Request(std::move(request_factory),
          [callback = std::move(callback)](
              Status status, network::URLResponsePtr response) mutable {
            RunUploadObjectCallback(std::move(callback), status,
                                    std::move(response));
          });
}

void CloudStorageImpl::DownloadObject(
    std::string auth_token,
    const std::string& key,
    std::function<void(Status status, uint64_t size, zx::socket data)>
        callback) {
  std::string url = GetDownloadUrl(key);

  Request(
      [auth_token = std::move(auth_token), url = std::move(url)] {
        network::URLRequestPtr request(network::URLRequest::New());
        request->url = url;
        request->method = "GET";
        request->auto_follow_redirects = true;
        if (!auth_token.empty()) {
          request->headers.push_back(MakeAuthorizationHeader(auth_token));
        }
        return request;
      },
      [this, callback = std::move(callback)](
          Status status, network::URLResponsePtr response) mutable {
        OnDownloadResponseReceived(std::move(callback), status,
                                   std::move(response));
      });
}

std::string CloudStorageImpl::GetDownloadUrl(fxl::StringView key) {
  FXL_DCHECK(key.find('/') == std::string::npos);
  return fxl::Concatenate({url_prefix_, key, "?alt=media"});
}

std::string CloudStorageImpl::GetUploadUrl(fxl::StringView key) {
  FXL_DCHECK(key.find('/') == std::string::npos);
  return fxl::Concatenate({url_prefix_, key});
}

void CloudStorageImpl::Request(
    std::function<network::URLRequestPtr()> request_factory,
    std::function<void(Status status, network::URLResponsePtr response)>
        callback) {
  requests_.emplace(network_service_->Request(
      std::move(request_factory),
      [this, callback = std::move(callback)](
          network::URLResponsePtr response) mutable {
        OnResponse(std::move(callback), std::move(response));
      }));
}

void CloudStorageImpl::OnResponse(
    std::function<void(Status status, network::URLResponsePtr response)>
        callback,
    network::URLResponsePtr response) {
  if (response->error) {
    FXL_LOG(ERROR) << response->url << " error "
                   << response->error->description;
    callback(Status::NETWORK_ERROR, std::move(response));
    return;
  }

  if (response->status_code == 404) {
    callback(Status::NOT_FOUND, std::move(response));
    return;
  }

  if (response->status_code != 200 && response->status_code != 204) {
    FXL_LOG(ERROR) << response->url << " error " << response->status_line;
    callback(Status::SERVER_ERROR, std::move(response));
    return;
  }

  callback(Status::OK, std::move(response));
}

void CloudStorageImpl::OnDownloadResponseReceived(
    const std::function<void(Status status, uint64_t size, zx::socket data)>
        callback,
    Status status,
    network::URLResponsePtr response) {
  if (status != Status::OK) {
    callback(status, 0u, zx::socket());
    return;
  }

  network::HttpHeaderPtr size_header =
      GetHeader(response->headers, kContentLengthHeader);
  if (!size_header) {
    callback(Status::PARSE_ERROR, 0u, zx::socket());
    return;
  }

  uint64_t expected_file_size;
  if (!fxl::StringToNumberWithError(size_header->value.get(),
                                    &expected_file_size)) {
    callback(Status::PARSE_ERROR, 0u, zx::socket());
    return;
  }

  network::URLBodyPtr body = std::move(response->body);
  FXL_DCHECK(body->is_stream());
  callback(Status::OK, expected_file_size, std::move(body->get_stream()));
}

}  // namespace gcs
