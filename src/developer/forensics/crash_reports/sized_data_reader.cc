// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/sized_data_reader.h"

#include <lib/syslog/cpp/macros.h>

namespace forensics {
namespace crash_reports {

crashpad::FileOperationResult SizedDataReader::Read(void* data, const size_t size) {
  if (offset_ >= data_.size()) {
    return 0;
  }

  // Can't read beyond the end of the buffer.
  const auto read_size = std::min(size, data_.size() - offset_);
  memcpy(data, &data_[offset_], read_size);
  offset_ += read_size;

  return read_size;
}

crashpad::FileOffset SizedDataReader::Seek(const crashpad::FileOffset offset, const int whence) {
  size_t base_offset{0u};

  switch (whence) {
    case SEEK_SET:
      base_offset = 0;
      break;
    case SEEK_CUR:
      base_offset = offset_;
      break;
    case SEEK_END:
      base_offset = data_.size();
      break;
    default:
      FX_LOGS(ERROR) << "Invalid whence " << whence;
      return -1;
  }

  offset_ = base_offset + offset;

  return offset_;
}

}  // namespace crash_reports
}  // namespace forensics
