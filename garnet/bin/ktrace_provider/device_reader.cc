// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ktrace_provider/device_reader.h"

#include <fcntl.h>
#include <lib/fdio/fdio.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/status.h>

#include <algorithm>

#include <src/lib/files/eintr_wrapper.h>

namespace ktrace_provider {

DeviceReader::DeviceReader() : Reader(buffer_, kChunkSize) {}

zx_status_t DeviceReader::Init() {
  auto [status, channel] = OpenKtraceReader();
  if (status == ZX_OK) {
    ktrace_reader_.Bind(std::move(channel));
  }
  return status;
}

std::tuple<zx_status_t, zx::channel> DeviceReader::OpenKtraceReader() {
  int fd = open(kKtraceReaderSvc, O_RDONLY);
  if (fd < 0) {
    FX_LOGS(ERROR) << "Failed to open " << kKtraceReaderSvc << ": errno=" << errno;
    return {ZX_ERR_IO, zx::channel()};
  }
  zx::channel channel;
  zx_status_t status = fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get " << kKtraceReaderSvc
                   << " channel: " << zx_status_get_string(status);
    return {ZX_ERR_IO, zx::channel()};
  }
  return {ZX_OK, std::move(channel)};
}

void DeviceReader::ReadMoreData() {
  memcpy(buffer_, current_, AvailableBytes());
  char* new_marker = buffer_ + AvailableBytes();

  while (new_marker < end_) {
    size_t read_size = std::distance(const_cast<const char*>(new_marker), end_);
    read_size = std::clamp(read_size, 0lu, static_cast<size_t>(fuchsia::tracing::kernel::MAX_BUF));
    zx_status_t out_status;
    std::vector<uint8_t> buf;
    auto status = ktrace_reader_->ReadAt(read_size, offset_, &out_status, &buf);
    if (status != ZX_OK || out_status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to read from ktrace reader status:" << status
                     << "out_status:" << out_status;
      break;
    }

    if (buf.size() == 0) {
      break;
    }

    memcpy(new_marker, buf.data(), buf.size());
    offset_ += buf.size();
    new_marker += buf.size();
  }

  marker_ = new_marker;
  current_ = buffer_;
}

}  // namespace ktrace_provider
