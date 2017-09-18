// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/src/ktrace_provider/reader.h"

#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/fxl/files/eintr_wrapper.h"

namespace ktrace_provider {
namespace {

constexpr char kTraceDev[] = "/dev/misc/ktrace";

}  // namespace

Reader::Reader() : fd_(open(kTraceDev, O_RDONLY)) {}

ktrace_header_t* Reader::ReadNextRecord() {
  if (AvailableBytes() < sizeof(ktrace_header_t))
    ReadMoreData();

  if (AvailableBytes() < sizeof(ktrace_header_t))
    return nullptr;

  auto record = reinterpret_cast<ktrace_header_t*>(current_);

  if (AvailableBytes() < KTRACE_LEN(record->tag))
    ReadMoreData();

  if (AvailableBytes() < KTRACE_LEN(record->tag))
    return nullptr;

  record = reinterpret_cast<ktrace_header_t*>(current_);
  current_ += KTRACE_LEN(record->tag);

  return record;
}

void Reader::ReadMoreData() {
  memcpy(buffer_, current_, AvailableBytes());
  marker_ = buffer_ + AvailableBytes();

  while (marker_ < end_) {
    int bytes_read =
        HANDLE_EINTR(read(fd_.get(), marker_, std::distance(marker_, end_)));

    if (bytes_read <= 0)
      break;

    marker_ += bytes_read;
  }

  current_ = buffer_;
}

}  // namespace ktrace_provider
