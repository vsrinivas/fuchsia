// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/media_service/network_reader_impl.h"

#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "lib/ftl/logging.h"

namespace mojo {
namespace media {

const char* NetworkReaderImpl::kContentLengthHeaderName = "Content-Length";
const char* NetworkReaderImpl::kAcceptRangesHeaderName = "Accept-Ranges";
const char* NetworkReaderImpl::kAcceptRangesHeaderBytesValue = "bytes";
const char* NetworkReaderImpl::kRangeHeaderName = "Range";

// static
std::shared_ptr<NetworkReaderImpl> NetworkReaderImpl::Create(
    const String& url,
    InterfaceRequest<SeekingReader> request,
    MediaFactoryService* owner) {
  return std::shared_ptr<NetworkReaderImpl>(
      new NetworkReaderImpl(url, request.Pass(), owner));
}

NetworkReaderImpl::NetworkReaderImpl(const String& url,
                                     InterfaceRequest<SeekingReader> request,
                                     MediaFactoryService* owner)
    : MediaFactoryService::Product<SeekingReader>(this, request.Pass(), owner),
      url_(url) {
  NetworkServicePtr network_service;

  ConnectToService(owner->shell(), "mojo:network_service",
                   GetProxy(&network_service));

  network_service->CreateURLLoader(GetProxy(&url_loader_));

  URLRequestPtr url_request(URLRequest::New());
  url_request->url = url_;
  url_request->method = "HEAD";

  url_loader_->Start(url_request.Pass(), [this](URLResponsePtr response) {
    // TODO(dalesat): Handle redirects.
    if (response->status_code != kStatusOk) {
      LOG(WARNING) << "HEAD response status code " << response->status_code;
      result_ = response->status_code == kStatusNotFound
                    ? MediaResult::NOT_FOUND
                    : MediaResult::UNKNOWN_ERROR;
      ready_.Occur();
      return;
    }

    for (const HttpHeaderPtr& header : response->headers) {
      if (header->name == kContentLengthHeaderName) {
        size_ = std::stoull(header->value);
      } else if (header->name == kAcceptRangesHeaderName &&
                 header->value == kAcceptRangesHeaderBytesValue) {
        can_seek_ = true;
      }
    }

    ready_.Occur();
  });
}

NetworkReaderImpl::~NetworkReaderImpl() {}

void NetworkReaderImpl::Describe(const DescribeCallback& callback) {
  ready_.When([this, callback]() { callback.Run(result_, size_, can_seek_); });
}

void NetworkReaderImpl::ReadAt(uint64_t position,
                               const ReadAtCallback& callback) {
  ready_.When([this, position, callback]() {
    if (result_ != MediaResult::OK) {
      callback.Run(result_, ScopedHandleBase<DataPipeConsumerHandle>());
      return;
    }

    if (!can_seek_ && position != 0) {
      callback.Run(MediaResult::INVALID_ARGUMENT,
                   ScopedHandleBase<DataPipeConsumerHandle>());
      return;
    }

    URLRequestPtr request(URLRequest::New());
    request->url = url_;
    request->method = "GET";

    if (position != 0) {
      std::ostringstream value;
      value << kAcceptRangesHeaderBytesValue << "=" << position << "-";

      HttpHeaderPtr header(HttpHeader::New());
      header->name = kRangeHeaderName;
      header->value = value.str();

      request->headers = Array<HttpHeaderPtr>::New(1).Pass();
      request->headers[0] = header.Pass();
    }

    url_loader_->Start(request.Pass(), [this,
                                        callback](URLResponsePtr response) {
      if (response->status_code != kStatusOk &&
          response->status_code != kStatusPartialContent) {
        LOG(WARNING) << "GET response status code " << response->status_code;
        result_ = MediaResult::UNKNOWN_ERROR;
        callback.Run(result_, ScopedHandleBase<DataPipeConsumerHandle>());
        return;
      }

      DCHECK(response->body.is_valid());
      callback.Run(result_, response->body.Pass());
    });
  });
}

}  // namespace media
}  // namespace mojo
