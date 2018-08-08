// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "upload_element_reader.h"

#include <utility>

#include "lib/fxl/logging.h"

namespace http {

UploadElementReader::UploadElementReader() : err_(ZX_OK) {}
UploadElementReader::~UploadElementReader() = default;

SocketUploadElementReader::SocketUploadElementReader(zx::socket socket)
    : socket_(std::move(socket)) {}

SocketUploadElementReader::~SocketUploadElementReader() {}

size_t SocketUploadElementReader::size() { return kUnknownSize; }

bool SocketUploadElementReader::ReadAvailable(std::ostream* os) {
  while (true) {
    size_t num_bytes = buf_.size();
    err_ = socket_.read(0u, buf_.data(), num_bytes, &num_bytes);
    if (err_ == ZX_ERR_SHOULD_WAIT) {
      err_ = socket_.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED,
                              zx::time::infinite(), nullptr);
      if (err_ == ZX_OK)
        continue;  // retry now that the socket is ready
    }

    if (err_ != ZX_OK) {
      // If the other end closes the socket, we get MX_ERR_PEER_CLOSED.
      if (err_ == ZX_ERR_PEER_CLOSED) {
        err_ = ZX_OK;
        return false;
      }
      FXL_VLOG(1) << "SocketUploadElementReader: result=" << err_;
      return false;
    }

    os->write(buf_.data(), num_bytes);
    if (!*os) {
      // TODO(toshik): better result code?
      err_ = ZX_ERR_BUFFER_TOO_SMALL;
      FXL_VLOG(1) << "SocketUploadElementReader: result=" << err_;
      return false;
    } else {
      return true;
    }
  }
}

VmoUploadElementReader::VmoUploadElementReader(zx::vmo vmo)
    : vmo_(std::move(vmo)), offset_(0) {
  err_ = vmo_.get_size(&size_);
}

VmoUploadElementReader::VmoUploadElementReader(zx::vmo vmo, uint64_t size)
    : vmo_(std::move(vmo)), size_(size), offset_(0) {}

VmoUploadElementReader::~VmoUploadElementReader() {}

size_t VmoUploadElementReader::size() { return size_; }

bool VmoUploadElementReader::ReadAvailable(std::ostream* os) {
  const size_t remaining_bytes = size_ - offset_;
  const size_t bytes_to_process = std::min(remaining_bytes, BUFSIZE);

  err_ = vmo_.read(buf_.data(), offset_, bytes_to_process);
  if (err_ != ZX_OK) {
    FXL_VLOG(1) << "VmoUploadElementReader: result=" << err_;
    return false;
  }

  if (bytes_to_process > 0) {
    os->write(buf_.data(), bytes_to_process);
    if (!*os) {
      FXL_VLOG(1) << "VmoUploadElementReader: Unable to write to stream.";
      // TODO(toshik): better result code?
      err_ = ZX_ERR_BUFFER_TOO_SMALL;
      return false;
    }
    offset_ += bytes_to_process;

    return true;
  } else {
    return false;
  }
}

}  // namespace http
