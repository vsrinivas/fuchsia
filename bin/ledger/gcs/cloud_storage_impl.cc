// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/gcs/cloud_storage_impl.h"

#include <fcntl.h>

#include <string>

#include "apps/ledger/src/glue/data_pipe/data_pipe.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/ftl/files/eintr_wrapper.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/file_descriptor.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/ascii.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/data_pipe/files.h"

namespace gcs {

namespace {

const char kContentLengthHeader[] = "content-length";

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

void RunUploadFileCallback(const std::function<void(Status)>& callback,
                           Status status,
                           network::URLResponsePtr response) {
  // A precondition failure means the object already exist.
  if (response->status_code == 412) {
    callback(Status::OBJECT_ALREADY_EXIST);
    return;
  }
  callback(status);
}

void OnFileWritten(const std::string& destination,
                   const std::function<void(Status)>& callback,
                   uint64_t expected_file_size,
                   bool success) {
  if (!success) {
    files::DeletePath(destination, false);
    callback(Status::UNKNOWN_ERROR);
    return;
  }

  uint64_t file_size;
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

CloudStorageImpl::CloudStorageImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                                   ledger::NetworkService* network_service,
                                   const std::string& bucket_name)
    : task_runner_(std::move(task_runner)),
      network_service_(std::move(network_service)),
      bucket_name_(bucket_name) {}

CloudStorageImpl::~CloudStorageImpl() {}

void CloudStorageImpl::UploadFile(const std::string& key,
                                  const std::string& source,
                                  const std::function<void(Status)>& callback) {
  uint64_t file_size;
  if (!files::GetFileSize(source, &file_size)) {
    callback(Status::UNKNOWN_ERROR);
    return;
  }

  ftl::UniqueFD fd(open(source.c_str(), O_RDONLY));
  if (!fd.is_valid()) {
    callback(Status::UNKNOWN_ERROR);
    return;
  }

  std::string url =
      "https://storage-upload.googleapis.com/" + bucket_name_ + "/" + key;

  auto request_factory = ftl::MakeCopyable([
    url, source, file_size, task_runner = task_runner_, fd = std::move(fd)
  ]() {
    network::URLRequestPtr request(network::URLRequest::New());
    request->url = url;
    request->method = "PUT";
    request->auto_follow_redirects = true;

    // Content-Length header.
    network::HttpHeaderPtr content_length_header = network::HttpHeader::New();
    content_length_header->name = kContentLengthHeader;
    content_length_header->value = ftl::NumberToString(file_size);
    request->headers.push_back(std::move(content_length_header));

    // x-goog-if-generation-match header. This ensures that files are never
    // overwritten.
    network::HttpHeaderPtr generation_match_header = network::HttpHeader::New();
    generation_match_header->name = "x-goog-if-generation-match";
    generation_match_header->value = "0";
    request->headers.push_back(std::move(generation_match_header));

    glue::DataPipe data_pipe;

    ftl::UniqueFD new_fd(dup(fd.get()));
    lseek(new_fd.get(), 0, SEEK_SET);
    mtl::CopyFromFileDescriptor(
        std::move(new_fd), std::move(data_pipe.producer_handle), task_runner,
        [](bool result, ftl::UniqueFD fd) {
          if (!result) {
            // An error while reading the file means that
            // the data sent to the server will not match
            // the content length header, and the server
            // will not accept the file.
            FTL_LOG(ERROR) << "Error when reading the data.";
          }
        });
    request->body = network::URLBody::New();
    request->body->set_stream(std::move(data_pipe.consumer_handle));
    return request;
  });

  Request(std::move(request_factory),
          [callback](Status status, network::URLResponsePtr response) {
            RunUploadFileCallback(std::move(callback), status,
                                  std::move(response));
          });
}

void CloudStorageImpl::DownloadFile(
    const std::string& key,
    const std::string& destination,
    const std::function<void(Status)>& callback) {
  std::string url =
      "https://storage-download.googleapis.com/" + bucket_name_ + "/" + key;

  Request(
      [url]() {
        network::URLRequestPtr request(network::URLRequest::New());
        request->url = url;
        request->method = "GET";
        request->auto_follow_redirects = true;
        return request;
      },
      [this, destination, callback](Status status,
                                    network::URLResponsePtr response) {
        OnDownloadResponseReceived(std::move(destination), std::move(callback),
                                   status, std::move(response));
      });
}

void CloudStorageImpl::Request(
    std::function<network::URLRequestPtr()>&& request_factory,
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
    network::URLResponsePtr response) {
  if (status != Status::OK) {
    callback(status);
    return;
  }

  network::HttpHeaderPtr size_header =
      GetHeader(response->headers, kContentLengthHeader);
  if (!size_header) {
    callback(Status::UNKNOWN_ERROR);
    return;
  }

  uint64_t expected_file_size;
  if (!ftl::StringToNumberWithError(size_header->value.get(),
                                    &expected_file_size)) {
    callback(Status::UNKNOWN_ERROR);
    return;
  }

  network::URLBodyPtr body = std::move(response->body);
  FTL_DCHECK(body->is_stream());

  ftl::UniqueFD fd(HANDLE_EINTR(creat(destination.c_str(), 0666)));
  if (!fd.is_valid()) {
    callback(Status::UNKNOWN_ERROR);
    return;
  }

  mtl::CopyToFileDescriptor(
      std::move(body->get_stream()), std::move(fd), task_runner_,
      [destination, callback, expected_file_size](bool success,
                                                  ftl::UniqueFD fd) {
        OnFileWritten(std::move(destination), std::move(callback),
                      expected_file_size, success);
      });
}

}  // namespace gcs
