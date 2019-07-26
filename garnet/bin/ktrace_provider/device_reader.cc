// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ktrace_provider/device_reader.h"

#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include <src/lib/files/eintr_wrapper.h>

namespace ktrace_provider {

namespace {
constexpr char kTraceDev[] = "/dev/misc/ktrace";
}  // namespace

DeviceReader::DeviceReader() : Reader(buffer_, kChunkSize), fd_(open(kTraceDev, O_RDONLY)) {}

void DeviceReader::ReadMoreData() {
  memcpy(buffer_, current_, AvailableBytes());
  char* new_marker = buffer_ + AvailableBytes();

  while (new_marker < end_) {
    int bytes_read = HANDLE_EINTR(
        read(fd_.get(), new_marker, std::distance(const_cast<const char*>(new_marker), end_)));

    if (bytes_read <= 0)
      break;

    new_marker += bytes_read;
  }

  marker_ = new_marker;
  current_ = buffer_;
}

}  // namespace ktrace_provider
