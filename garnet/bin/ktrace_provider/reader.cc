// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ktrace_provider/reader.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zircon-internal/ktrace.h>
#include <zircon/assert.h>

namespace ktrace_provider {

Reader::Reader(const char* buffer, size_t buffer_size)
    : current_(buffer), marker_(buffer), end_(buffer + buffer_size) {
  // Ensure initial buffer is correctly aligned.
  ZX_ASSERT(reinterpret_cast<uintptr_t>(buffer) % alignof(ktrace_header_t) == 0);
}

const ktrace_header_t* Reader::ReadNextRecord() {
  if (AvailableBytes() < sizeof(ktrace_header_t)) {
    ReadMoreData();
  }

  if (AvailableBytes() < sizeof(ktrace_header_t)) {
    FX_VLOGS(10) << "No more records";
    return nullptr;
  }

  auto record = reinterpret_cast<const ktrace_header_t*>(current_);

  if (AvailableBytes() < KTRACE_LEN(record->tag)) {
    ReadMoreData();
  }

  if (AvailableBytes() < KTRACE_LEN(record->tag)) {
    FX_VLOGS(10) << "No more records, incomplete last record";
    return nullptr;
  }

  record = reinterpret_cast<const ktrace_header_t*>(current_);

  uint32_t len = KTRACE_LEN(record->tag);

  // A record with a length of zero is illegal.  If we encounter such a thing,
  // we are in trouble. Either the buffer coming from the kernel is corrupt, or
  // we have lost sync with the record stream somehow.  Either way, we cannot
  // make any more progress if we don't know how far to advance.  Report a
  // warning and get out.
  if (!len) {
    FX_LOGS(WARNING) << "Found 0-length record.  Reading cannot continue.";
    return nullptr;
  }

  current_ += len;
  number_bytes_read_ += len;
  number_records_read_ += 1;

  FX_VLOGS(10) << "Importing ktrace event 0x" << std::hex << KTRACE_EVENT(record->tag) << ", size "
               << std::dec << KTRACE_LEN(record->tag);

  return record;
}

}  // namespace ktrace_provider
