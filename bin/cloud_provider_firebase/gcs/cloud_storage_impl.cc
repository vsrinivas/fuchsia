// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/gcs/cloud_storage_impl.h"

#include <fcntl.h>
#include <string>

#include <lib/fit/function.h>

#include "lib/fidl/cpp/array.h"
#include "lib/fsl/socket/files.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fsl/vmo/sized_vmo.h"
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
#include "peridot/lib/socket/socket_pair.h"

namespace gcs {

namespace http = ::fuchsia::net::oldhttp;

namespace {

const char kAuthorizationHeader[] = "authorization";
const char kContentLengthHeader[] = "content-length";

constexpr fxl::StringView kApiEndpoint =
    "https://firebasestorage.googleapis.com/v0/b/";
constexpr fxl::StringView kBucketNameSuffix = ".appspot.com";

http::HttpHeaderPtr GetHeader(const fidl::VectorPtr<http::HttpHeader>& headers,
                              const std::string& header_name) {
  for (const auto& header : *headers) {
    if (fxl::EqualsCaseInsensitiveASCII(header.name.get(), header_name)) {
      auto result = http::HttpHeader::New();
      fidl::Clone(header, result.get());
      return result;
    }
  }
  return nullptr;
}

http::HttpHeader MakeAuthorizationHeader(const std::string& auth_token) {
  http::HttpHeader authorization_header;
  authorization_header.name = kAuthorizationHeader;
  authorization_header.value = "Bearer " + auth_token;
  return authorization_header;
}

void RunUploadObjectCallback(fit::function<void(Status)> callback,
                             Status status, http::URLResponse response) {
  // A precondition failure means the object already exist.
  if (response.status_code == 412) {
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

CloudStorageImpl::CloudStorageImpl(
    network_wrapper::NetworkWrapper* network_wrapper,
    const std::string& firebase_id, const std::string& cloud_prefix)
    : network_wrapper_(network_wrapper),
      url_prefix_(GetUrlPrefix(firebase_id, cloud_prefix)) {}

CloudStorageImpl::~CloudStorageImpl() {}

void CloudStorageImpl::UploadObject(std::string auth_token,
                                    const std::string& key, fsl::SizedVmo data,
                                    fit::function<void(Status)> callback) {
  std::string url = GetUploadUrl(key);

  auto request_factory = fxl::MakeCopyable([auth_token = std::move(auth_token),
                                            url = std::move(url),
                                            data = std::move(data)] {
    http::URLRequest request;
    request.url = url;
    request.method = "POST";
    request.auto_follow_redirects = true;

    // Authorization header.
    if (!auth_token.empty()) {
      request.headers.push_back(MakeAuthorizationHeader(auth_token));
    }

    // Content-Length header.
    http::HttpHeader content_length_header;
    content_length_header.name = kContentLengthHeader;
    content_length_header.value = fxl::NumberToString(data.size());
    request.headers.push_back(std::move(content_length_header));

    fsl::SizedVmo duplicated_data;
    zx_status_t status =
        data.Duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ, &duplicated_data);
    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "Unable to duplicate a vmo. Status: " << status;
      return http::URLRequest();
    }
    request.body = http::URLBody::New();
    request.body->set_sized_buffer(std::move(duplicated_data).ToTransport());
    return request;
  });

  Request(std::move(request_factory), [callback = std::move(callback)](
                                          Status status,
                                          http::URLResponse response) mutable {
    RunUploadObjectCallback(std::move(callback), status, std::move(response));
  });
}

void CloudStorageImpl::DownloadObject(
    std::string auth_token, const std::string& key,
    fit::function<void(Status status, uint64_t size, zx::socket data)>
        callback) {
  std::string url = GetDownloadUrl(key);

  Request(
      [auth_token = std::move(auth_token), url = std::move(url)] {
        http::URLRequest request;
        request.url = url;
        request.method = "GET";
        request.auto_follow_redirects = true;
        if (!auth_token.empty()) {
          request.headers.push_back(MakeAuthorizationHeader(auth_token));
        }
        return request;
      },
      [this, callback = std::move(callback)](
          Status status, http::URLResponse response) mutable {
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
    fit::function<http::URLRequest()> request_factory,
    fit::function<void(Status status, http::URLResponse response)> callback) {
  requests_.emplace(network_wrapper_->Request(
      std::move(request_factory), [this, callback = std::move(callback)](
                                      http::URLResponse response) mutable {
        OnResponse(std::move(callback), std::move(response));
      }));
}

void CloudStorageImpl::OnResponse(
    fit::function<void(Status status, http::URLResponse response)> callback,
    http::URLResponse response) {
  if (response.error) {
    FXL_LOG(ERROR) << response.url << " error " << response.error->description;
    callback(Status::NETWORK_ERROR, std::move(response));
    return;
  }

  if (response.status_code == 404) {
    callback(Status::NOT_FOUND, std::move(response));
    return;
  }

  if (response.status_code != 200 && response.status_code != 204) {
    FXL_LOG(ERROR) << response.url << " error " << response.status_line;
    callback(Status::SERVER_ERROR, std::move(response));
    return;
  }

  callback(Status::OK, std::move(response));
}

void CloudStorageImpl::OnDownloadResponseReceived(
    fit::function<void(Status status, uint64_t size, zx::socket data)> callback,
    Status status, http::URLResponse response) {
  if (status != Status::OK) {
    callback(status, 0u, zx::socket());
    return;
  }

  http::HttpHeaderPtr size_header =
      GetHeader(response.headers, kContentLengthHeader);
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

  http::URLBodyPtr body = std::move(response.body);
  FXL_DCHECK(body->is_stream());
  callback(Status::OK, expected_file_size, std::move(body->stream()));
}

}  // namespace gcs
