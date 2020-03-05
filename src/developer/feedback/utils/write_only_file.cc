// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/write_only_file.h"

#include <trace/event.h>

namespace feedback {
WriteOnlyFile::WriteOnlyFile(FileSize capacity) : capacity_(capacity) {}

bool WriteOnlyFile::Open(const std::string& path) {
  TRACE_DURATION("feedback:io", "WriteOnlyFile::Open");

  out_.open(path, std::ofstream::out | std::ofstream::trunc);
  return out_.is_open();
}

bool WriteOnlyFile::Write(const std::string& str) {
  TRACE_DURATION("feedback:io", "WriteOnlyFile::Write", "string_size", str.size());

  if (!out_.is_open() || str.size() > BytesRemaining()) {
    return false;
  }

  TRACE_DURATION_BEGIN("feedback:io", "WriteOnlyFile::Write::write", "string_size", str.size());
  out_.write(str.c_str(), str.size());
  TRACE_DURATION_END("feedback:io", "WriteOnlyFile::Write::write", "string_size", str.size());

  capacity_ -= str.size();
  return str.size();
}

uint64_t WriteOnlyFile::BytesRemaining() const { return capacity_.to_bytes(); }

}  // namespace feedback
