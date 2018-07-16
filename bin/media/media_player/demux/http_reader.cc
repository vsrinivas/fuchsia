// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/demux/http_reader.h"

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <lib/async/default.h>

#include "garnet/bin/http/http_errors.h"
#include "lib/component/cpp/connect.h"
#include "lib/fxl/logging.h"

namespace media_player {

namespace http = ::fuchsia::net::oldhttp;

namespace {

const char* kContentLengthHeaderName = "Content-Length";
const char* kAcceptRangesHeaderName = "Accept-Ranges";
const char* kAcceptRangesHeaderBytesValue = "bytes";
const char* kRangeHeaderName = "Range";

constexpr uint32_t kStatusOk = 200u;
constexpr uint32_t kStatusPartialContent = 206u;
constexpr uint32_t kStatusNotFound = 404u;

}  // namespace

// static
std::shared_ptr<HttpReader> HttpReader::Create(
    component::StartupContext* startup_context, const std::string& url) {
  return std::make_shared<HttpReader>(startup_context, url);
}

HttpReader::HttpReader(component::StartupContext* startup_context,
                       const std::string& url)
    : url_(url), ready_(async_get_default_dispatcher()) {
  http::HttpServicePtr network_service =
      startup_context->ConnectToEnvironmentService<http::HttpService>();

  network_service->CreateURLLoader(url_loader_.NewRequest());

  http::URLRequest url_request;
  url_request.url = url_;
  url_request.method = "HEAD";
  url_request.auto_follow_redirects = true;

  url_loader_->Start(std::move(url_request), [this](
                                                 http::URLResponse response) {
    if (response.error) {
      FXL_LOG(ERROR) << "HEAD response error " << response.error->code << " "
                     << (response.error->description
                             ? response.error->description
                             : "<no description>");
      result_ = response.error->code == ::http::HTTP_ERR_NAME_NOT_RESOLVED
                    ? Result::kNotFound
                    : Result::kUnknownError;
      ready_.Occur();
      return;
    }

    if (response.status_code != kStatusOk) {
      FXL_LOG(ERROR) << "HEAD response status code " << response.status_code;
      result_ = response.status_code == kStatusNotFound ? Result::kNotFound
                                                        : Result::kUnknownError;
      ready_.Occur();
      return;
    }

    for (const http::HttpHeader& header : *response.headers) {
      if (header.name == kContentLengthHeaderName) {
        size_ = std::stoull(header.value);
      } else if (header.name == kAcceptRangesHeaderName &&
                 header.value == kAcceptRangesHeaderBytesValue) {
        can_seek_ = true;
      }
    }

    ready_.Occur();
  });
}

HttpReader::~HttpReader() {}

void HttpReader::Describe(DescribeCallback callback) {
  ready_.When([this, callback = std::move(callback)]() {
    callback(result_, size_, can_seek_);
  });
}

void HttpReader::ReadAt(size_t position, uint8_t* buffer, size_t bytes_to_read,
                        ReadAtCallback callback) {
  ready_.When([this, position, buffer, bytes_to_read,
               callback = std::move(callback)]() mutable {
    if (result_ != Result::kOk) {
      callback(result_, 0);
      return;
    }

    if (!can_seek_ && position != 0) {
      callback(Result::kInvalidArgument, 0);
      return;
    }

    read_at_position_ = position;
    read_at_buffer_ = buffer;

    if (read_at_position_ + bytes_to_read > size_) {
      read_at_bytes_to_read_ = size_ - read_at_position_;
    } else {
      read_at_bytes_to_read_ = bytes_to_read;
    }

    read_at_bytes_remaining_ = read_at_bytes_to_read_;
    read_at_callback_ = std::move(callback);

    if (!socket_ || socket_position_ != read_at_position_) {
      socket_.reset();
      socket_position_ = kUnknownSize;
      LoadAndReadFromSocket();
      return;
    }

    ReadFromSocket();
  });
}

void HttpReader::ReadFromSocket() {
  while (true) {
    size_t byte_count = 0;
    zx_status_t status = socket_.read(0u, read_at_buffer_,
                                      read_at_bytes_remaining_, &byte_count);

    if (status == ZX_ERR_SHOULD_WAIT) {
      waiter_ = std::make_unique<async::Wait>(
          socket_.get(), ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED);

      waiter_->set_handler([this](async_dispatcher_t* dispatcher, async::Wait* wait,
                                  zx_status_t status,
                                  const zx_packet_signal_t* signal) {
        if (status != ZX_OK) {
          if (status != ZX_ERR_CANCELED) {
            FXL_LOG(ERROR) << "AsyncWait failed, status " << status;
          }

          FailReadAt(status);
          return;
        }

        ReadFromSocket();
      });

      waiter_->Begin(async_get_default_dispatcher());

      break;
    }

    waiter_.reset();

    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "zx::socket::read failed, status " << status;
      FailReadAt(status);
      break;
    }

    read_at_buffer_ += byte_count;
    read_at_bytes_remaining_ -= byte_count;
    socket_position_ += byte_count;

    if (read_at_bytes_remaining_ == 0) {
      CompleteReadAt(Result::kOk, read_at_bytes_to_read_);
      break;
    }
  }
}

void HttpReader::CompleteReadAt(Result result, size_t bytes_read) {
  ReadAtCallback read_at_callback;
  read_at_callback_.swap(read_at_callback);
  read_at_callback(result, bytes_read);
}

void HttpReader::FailReadAt(zx_status_t status) {
  switch (status) {
    case ZX_ERR_PEER_CLOSED:
      FailReadAt(Result::kPeerClosed);
      break;
    case ZX_ERR_CANCELED:
      FailReadAt(Result::kCancelled);
      break;
    // TODO(dalesat): Expect more statuses here.
    default:
      FXL_LOG(ERROR) << "Unexpected status " << status;
      FailReadAt(Result::kUnknownError);
      break;
  }
}

void HttpReader::FailReadAt(Result result) {
  result_ = result;
  socket_.reset();
  socket_position_ = kUnknownSize;
  CompleteReadAt(result_, 0);
}

void HttpReader::LoadAndReadFromSocket() {
  FXL_DCHECK(!socket_);

  if (!can_seek_ && read_at_position_ != 0) {
    FailReadAt(Result::kInvalidArgument);
    return;
  }

  http::URLRequest request;
  request.url = url_;
  request.method = "GET";

  if (read_at_position_ != 0) {
    std::ostringstream value;
    value << kAcceptRangesHeaderBytesValue << "=" << read_at_position_ << "-";

    http::HttpHeader header;
    header.name = kRangeHeaderName;
    header.value = value.str();
    request.headers.push_back(std::move(header));
  }

  url_loader_->Start(std::move(request), [this](http::URLResponse response) {
    if (response.status_code != kStatusOk &&
        response.status_code != kStatusPartialContent) {
      FXL_LOG(WARNING) << "GET response status code " << response.status_code;
      FailReadAt(Result::kUnknownError);
      return;
    }

    socket_ = std::move(response.body->stream());
    socket_position_ = read_at_position_;

    ReadFromSocket();
  });
}

}  // namespace media_player
