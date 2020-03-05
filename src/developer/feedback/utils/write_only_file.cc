// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/write_only_file.h"

#include <fcntl.h>
#include <unistd.h>

#include <trace/event.h>

namespace feedback {

WriteOnlyFile::WriteOnlyFile(FileSize capacity)
    : buf_p_(&buf_[0]), capacity_(capacity), capacity_remaining_(FileSize::Bytes(0)) {}

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

  Flush();
  close(fd_);

  fd_ = -1;
  buf_p_ = &buf_[0];
  capacity_remaining_ = FileSize::Bytes(0);
}

bool WriteOnlyFile::Write(const std::string& str) {
  TRACE_DURATION("feedback:io", "WriteOnlyFile::Write", "string_size", str.size());

  if (fd_ < 0 || str.size() > BytesRemaining()) {
    return false;
  }

  size_t bytes_remaining = str.size();
  const char* str_p = str.c_str();

  while (bytes_remaining > 0) {
    const size_t to_write = std::min(bytes_remaining, static_cast<size_t>(&buf_end_ - buf_p_));

    memcpy(buf_p_, str_p, to_write);

    buf_p_ += to_write;
    bytes_remaining -= to_write;

    if (buf_p_ == &buf_end_) {
      Flush();
    }
  }

  capacity_remaining_ -= str.size();

  return str.size();
}

uint64_t WriteOnlyFile::BytesRemaining() const { return capacity_remaining_.to_bytes(); }

void WriteOnlyFile::Flush() {
  TRACE_DURATION("feedback:io", "WriteOnlyFile::Flush");

  if (fd_ < 0) {
    return;
  }

  write(fd_, buf_, buf_p_ - buf_);
  buf_p_ = &buf_[0];
}

}  // namespace feedback
