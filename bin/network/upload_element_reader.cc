// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "upload_element_reader.h"

#include <utility>

#include "lib/fxl/logging.h"

namespace network {

SocketUploadElementReader::SocketUploadElementReader(zx::socket socket)
    : socket_(std::move(socket)) {}

SocketUploadElementReader::~SocketUploadElementReader() {}

zx_status_t SocketUploadElementReader::ReadAll(std::ostream* os) {
  zx_status_t result = ZX_OK;

  while (true) {
    size_t num_bytes = buf_.size();
    result = socket_.read(0u, buf_.data(), num_bytes, &num_bytes);
    if (result == ZX_ERR_SHOULD_WAIT) {
      result = socket_.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED,
                                ZX_TIME_INFINITE, nullptr);
      if (result == ZX_OK)
        continue;  // retry now that the socket is ready
    }

    if (result != ZX_OK) {
      // If the other end closes the socket,
      // we get ZX_ERR_PEER_CLOSED.
      if (result == ZX_ERR_PEER_CLOSED) {
        result = ZX_OK;
        break;
      }
      FXL_VLOG(1) << "SocketUploadElementReader: result=" << result;
      break;
    }

    os->write(buf_.data(), num_bytes);
    if (!*os) {
      // TODO(toshik): better result code?
      result = ZX_ERR_BUFFER_TOO_SMALL;
      FXL_VLOG(1) << "SocketUploadElementReader: result=" << result;
      break;
    }
  }

  return result;
}

VmoUploadElementReader::VmoUploadElementReader(zx::vmo vmo)
    : vmo_(std::move(vmo)) {}

VmoUploadElementReader::~VmoUploadElementReader() {}

zx_status_t VmoUploadElementReader::ReadAll(std::ostream* os) {
  uint64_t remaining;
  zx_status_t result = vmo_.get_size(&remaining);
  if (result != ZX_OK) {
    FXL_VLOG(1) << "VmoUploadELementReader: result=" << result;
    return result;
  }

  uint64_t offset = 0;
  while (remaining > 0) {
    size_t num_bytes = buf_.size();
    result = vmo_.read(buf_.data(), offset, num_bytes, &num_bytes);
    if (result != ZX_OK) {
      FXL_VLOG(1) << "VmoUploadELementReader: result=" << result;
      return result;
    }

    os->write(buf_.data(), num_bytes);
    if (!*os) {
      FXL_VLOG(1) << "VmoUploadElementReader: Unable to write to stream.";
      // TODO(toshik): better result code?
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    offset += num_bytes;
    remaining -= num_bytes;
  }

  return ZX_OK;
}

}  // namespace network
