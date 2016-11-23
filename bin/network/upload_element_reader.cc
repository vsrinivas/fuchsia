// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "upload_element_reader.h"

#include <utility>

#include "lib/ftl/logging.h"

namespace network {

DatapipeUploadElementReader::DatapipeUploadElementReader(
    mx::datapipe_consumer pipe)
    : pipe_(std::move(pipe)) {}

DatapipeUploadElementReader::~DatapipeUploadElementReader() {}

mx_status_t DatapipeUploadElementReader::ReadAll(std::ostream* os) {
  mx_status_t result = NO_ERROR;

  while (true) {
    mx_size_t num_bytes = buf_.size();
    result = pipe_.read(0u, buf_.data(), num_bytes, &num_bytes);
    if (result == ERR_SHOULD_WAIT) {
      result = pipe_.wait_one(MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                              MX_TIME_INFINITE, nullptr);
      if (result == NO_ERROR)
        continue;  // retry now that the data pipe is ready
    }

    if (result != NO_ERROR) {
      // If the other end closes the data pipe,
      // we get ERR_REMOTE_CLOSED.
      if (result == ERR_REMOTE_CLOSED) {
        result = NO_ERROR;
        break;
      }
      FTL_LOG(ERROR) << "DatapipeUploadELementReader: result=" << result;
      break;
    }

    os->write(buf_.data(), num_bytes);
    if (!*os) {
      // TODO(toshik): better result code?
      result = ERR_BUFFER_TOO_SMALL;
      FTL_LOG(ERROR) << "DatapipeUploadElementReader: result=" << result;
      break;
    }
  }

  return result;
}

VmoUploadElementReader::VmoUploadElementReader(mx::vmo vmo)
    : vmo_(std::move(vmo)) {}

VmoUploadElementReader::~VmoUploadElementReader() {}

mx_status_t VmoUploadElementReader::ReadAll(std::ostream* os) {
  mx_status_t result = NO_ERROR;

  uint64_t offset = 0;
  while (true) {
    mx_size_t num_bytes = buf_.size();
    result = vmo_.read(buf_.data(), offset, num_bytes, &num_bytes);
    if (result != NO_ERROR) {
      if (result == ERR_OUT_OF_RANGE) {
        result = NO_ERROR;
        break;
      }
      FTL_LOG(ERROR) << "VmoUploadELementReader: result=" << result;
      break;
    }

    os->write(buf_.data(), num_bytes);
    if (!*os) {
      // TODO(toshik): better result code?
      result = ERR_BUFFER_TOO_SMALL;
      FTL_LOG(ERROR) << "VmoUploadElementReader: result=" << result;
      break;
    }
    offset += num_bytes;
  }

  return result;
}

}  // namespace network
