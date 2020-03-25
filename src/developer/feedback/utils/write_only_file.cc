// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/write_only_file.h"

#include <fcntl.h>
#include <unistd.h>

#include <trace/event.h>

namespace feedback {

WriteOnlyFile::WriteOnlyFile(FileSize capacity)
    : capacity_(capacity), capacity_remaining_(FileSize::Bytes(0)) {}

WriteOnlyFile::~WriteOnlyFile() { Close(); }

bool WriteOnlyFile::Open(const std::string& path) {
  TRACE_DURATION("feedback:io", "WriteOnlyFile::Open");

  fd_ = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);

  if (fd_ < 0) {
    return false;
  } else {
    capacity_remaining_ = capacity_;
    return true;
  }
}

void WriteOnlyFile::Close() {
  TRACE_DURATION("feedback:io", "WriteOnlyFile::Close");

  close(fd_);

  fd_ = -1;
  capacity_remaining_ = FileSize::Bytes(0);
}

bool WriteOnlyFile::Write(const std::string& str) {
  TRACE_DURATION("feedback:io", "WriteOnlyFile::Write", "string_size", str.size());

  if (fd_ < 0 || str.size() > BytesRemaining()) {
    return false;
  }

  write(fd_, str.c_str(), str.size());
  capacity_remaining_ -= str.size();

  return str.size();
}

uint64_t WriteOnlyFile::BytesRemaining() const { return capacity_remaining_.to_bytes(); }

}  // namespace feedback
