// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "upload_element_reader.h"

#include <utility>

#include "lib/ftl/logging.h"

namespace network {

SocketUploadElementReader::SocketUploadElementReader(mx::socket socket)
    : socket_(std::move(socket)) {}

SocketUploadElementReader::~SocketUploadElementReader() {}

mx_status_t SocketUploadElementReader::ReadAll(std::ostream* os) {
  mx_status_t result = MX_OK;

  while (true) {
    size_t num_bytes = buf_.size();
    result = socket_.read(0u, buf_.data(), num_bytes, &num_bytes);
    if (result == MX_ERR_SHOULD_WAIT) {
      result = socket_.wait_one(MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED,
                                MX_TIME_INFINITE, nullptr);
      if (result == MX_OK)
        continue;  // retry now that the socket is ready
    }

    if (result != MX_OK) {
      // If the other end closes the socket,
      // we get MX_ERR_PEER_CLOSED.
      if (result == MX_ERR_PEER_CLOSED) {
        result = MX_OK;
        break;
      }
      FTL_VLOG(1) << "SocketUploadElementReader: result=" << result;
      break;
    }

    os->write(buf_.data(), num_bytes);
    if (!*os) {
      // TODO(toshik): better result code?
      result = MX_ERR_BUFFER_TOO_SMALL;
      FTL_VLOG(1) << "SocketUploadElementReader: result=" << result;
      break;
    }
  }

  return result;
}

VmoUploadElementReader::VmoUploadElementReader(mx::vmo vmo)
    : vmo_(std::move(vmo)) {}

VmoUploadElementReader::~VmoUploadElementReader() {}

mx_status_t VmoUploadElementReader::ReadAll(std::ostream* os) {
  uint64_t remaining;
  mx_status_t result = vmo_.get_size(&remaining);
  if (result != MX_OK) {
    FTL_VLOG(1) << "VmoUploadELementReader: result=" << result;
    return result;
  }

  uint64_t offset = 0;
  while (remaining > 0) {
    size_t num_bytes = buf_.size();
    result = vmo_.read(buf_.data(), offset, num_bytes, &num_bytes);
    if (result != MX_OK) {
      FTL_VLOG(1) << "VmoUploadELementReader: result=" << result;
      return result;
    }

    os->write(buf_.data(), num_bytes);
    if (!*os) {
      FTL_VLOG(1) << "VmoUploadElementReader: Unable to write to stream.";
      // TODO(toshik): better result code?
      return MX_ERR_BUFFER_TOO_SMALL;
    }
    offset += num_bytes;
    remaining -= num_bytes;
  }

  return MX_OK;
}

}  // namespace network
