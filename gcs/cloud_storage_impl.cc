// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/gcs/cloud_storage_impl.h"

#include <fcntl.h>

#include <string>

#include "lib/ftl/files/eintr_wrapper.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/file_descriptor.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/ascii.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/data_pipe/files.h"
#include "mojo/public/cpp/bindings/array.h"
#include "mojo/public/cpp/system/data_pipe.h"

namespace gcs {

namespace {

const char kContentLengthHeader[] = "content-length";

mojo::HttpHeaderPtr GetHeader(const mojo::Array<mojo::HttpHeaderPtr>& headers,
                              const std::string& header_name) {
  for (const auto& header : headers.storage()) {
    if (ftl::EqualsCaseInsensitiveASCII(header->name.get(), header_name)) {
      return header.Clone();
    }
  }
  return nullptr;
}

void RunUploadFileCallback(const std::function<void(Status)>& callback,
                           Status status,
                           mojo::URLResponsePtr response) {
  // A precondition failure means the object already exist.
  if (response->status_code == 412) {
    callback(Status::OBJECT_ALREADY_EXIST);
    return;
  }
  callback(status);
}

void OnFileWritten(const std::string& destination,
                   const std::function<void(Status)>& callback,
                   int64_t expected_file_size,
                   bool success) {
  if (!success) {
    files::DeletePath(destination, false);
    callback(Status::UNKNOWN_ERROR);
    return;
  }

  int64_t file_size;
  if (!files::GetFileSize(destination, &file_size)) {
    files::DeletePath(destination, false);
    callback(Status::UNKNOWN_ERROR);
    return;
  }

  if (file_size != expected_file_size) {
    files::DeletePath(destination, false);
    callback(Status::UNKNOWN_ERROR);
    return;
  }

  callback(Status::OK);
}

}  // namespace

CloudStorageImpl::CloudStorageImpl(ftl::TaskRunner* task_runner,
                                   mojo::NetworkServicePtr network_service,
                                   const std::string& bucket_name)
    : task_runner_(task_runner),
      network_service_(std::move(network_service)),
      bucket_name_(bucket_name) {}

CloudStorageImpl::~CloudStorageImpl() {}

void CloudStorageImpl::UploadFile(const std::string& key,
                                  const std::string& source,
                                  const std::function<void(Status)>& callback) {
  int64_t file_size;
  if (!files::GetFileSize(source, &file_size)) {
    callback(Status::UNKNOWN_ERROR);
    return;
  }

  std::string url =
      "https://storage-upload.googleapis.com/" + bucket_name_ + "/" + key;
  mojo::URLRequestPtr request(mojo::URLRequest::New());
  request->url = url;
  request->method = "PUT";
  request->auto_follow_redirects = true;

  // Content-Length header.
  mojo::HttpHeaderPtr content_length_header = mojo::HttpHeader::New();
  content_length_header->name = kContentLengthHeader;
  content_length_header->value = ftl::NumberToString(file_size);
  request->headers.push_back(std::move(content_length_header));

  // x-goog-if-generation-match header. This ensures that files are never
  // overwritten.
  mojo::HttpHeaderPtr generation_match_header = mojo::HttpHeader::New();
  generation_match_header->name = "x-goog-if-generation-match";
  generation_match_header->value = "0";
  request->headers.push_back(std::move(generation_match_header));

  mojo::DataPipe data_pipe;

  ftl::UniqueFD fd(open(source.c_str(), O_RDONLY));
  if (!fd.is_valid()) {
    callback(Status::UNKNOWN_ERROR);
    return;
  }

  mtl::CopyFromFileDescriptor(
      std::move(fd), std::move(data_pipe.producer_handle), task_runner_,
      [](bool result) {
        if (!result) {
          // An error while reading the file means that
          // the data sent to the server will not match
          // the content length header, and the server
          // will not accept the file.
          FTL_LOG(ERROR) << "Error when reading the data.";
        }
      });
  request->body.push_back(std::move(data_pipe.consumer_handle));

  Request(std::move(request), [callback](Status status,
                                         mojo::URLResponsePtr response) {
    RunUploadFileCallback(std::move(callback), status, std::move(response));
  });
}

void CloudStorageImpl::DownloadFile(
    const std::string& key,
    const std::string& destination,
    const std::function<void(Status)>& callback) {
  std::string url =
      "https://storage-download.googleapis.com/" + bucket_name_ + "/" + key;
  mojo::URLRequestPtr request(mojo::URLRequest::New());
  request->url = url;
  request->method = "GET";
  request->auto_follow_redirects = true;

  Request(
      std::move(request), [this, destination, callback](
                              Status status, mojo::URLResponsePtr response) {
        OnDownloadResponseReceived(std::move(destination), std::move(callback),
                                   status, std::move(response));
      });
}

void CloudStorageImpl::Request(
    mojo::URLRequestPtr request,
    const std::function<void(Status status, mojo::URLResponsePtr response)>&
        callback) {
  mojo::URLLoaderPtr url_loader;
  network_service_->CreateURLLoader(GetProxy(&url_loader));
  mojo::URLLoader* url_loader_ptr = url_loader.get();

  url_loader->Start(request.Pass(), [this, callback, url_loader_ptr](
                                        mojo::URLResponsePtr response) {
    OnResponse(std::move(callback), url_loader_ptr, std::move(response));
  });
  loaders_.push_back(std::move(url_loader));
}

void CloudStorageImpl::OnResponse(
    const std::function<void(Status status, mojo::URLResponsePtr response)>&
        callback,
    mojo::URLLoader* url_loader,
    mojo::URLResponsePtr response) {
  // Clear loader.
  loaders_.erase(std::find_if(loaders_.begin(), loaders_.end(),
                              [url_loader](const mojo::URLLoaderPtr& l) {
                                return l.get() == url_loader;
                              }));

  if (response->error) {
    FTL_LOG(ERROR) << response->url << " error "
                   << response->error->description;
    callback(Status::UNKNOWN_ERROR, std::move(response));
    return;
  }

  if (response->status_code != 200 && response->status_code != 204) {
    FTL_LOG(ERROR) << response->url << " error " << response->status_line;
    callback(Status::UNKNOWN_ERROR, std::move(response));
    return;
  }

  callback(Status::OK, std::move(response));
}

void CloudStorageImpl::OnDownloadResponseReceived(
    const std::string& destination,
    const std::function<void(Status)>& callback,
    Status status,
    mojo::URLResponsePtr response) {
  if (status != Status::OK) {
    callback(status);
    return;
  }

  mojo::HttpHeaderPtr size_header =
      GetHeader(response->headers, kContentLengthHeader);
  if (!size_header) {
    callback(Status::UNKNOWN_ERROR);
    return;
  }

  int64_t expected_file_size;
  if (!ftl::StringToNumberWithError(size_header->value.get(),
                                    &expected_file_size)) {
    callback(Status::UNKNOWN_ERROR);
    return;
  }

  auto body = std::move(response->body);
  ftl::UniqueFD fd(HANDLE_EINTR(creat(destination.c_str(), 0666)));
  if (!fd.is_valid()) {
    callback(Status::UNKNOWN_ERROR);
    return;
  }

  mtl::CopyToFileDescriptor(
      std::move(body), std::move(fd), task_runner_,
      [destination, callback, expected_file_size](bool success) {
        OnFileWritten(std::move(destination), std::move(callback),
                      expected_file_size, success);
      });
}

}  // namespace gcs
