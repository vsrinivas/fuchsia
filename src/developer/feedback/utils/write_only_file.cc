// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/write_only_file.h"

#include <cstring>

namespace feedback {

WriteOnlyFile::WriteOnlyFile(FileSize capacity) : capacity_(capacity) {}

bool WriteOnlyFile::Open(const std::string& path) {
  out_.open(path, std::ofstream::out | std::ofstream::trunc);
  return out_.is_open();
}

bool WriteOnlyFile::Write(const std::string& str) {
  if (!out_.is_open() || str.size() > BytesRemaining()) {
    return false;
  }

  out_.write(str.c_str(), str.size());

  capacity_ -= str.size();
  return str.size();
}

uint64_t WriteOnlyFile::BytesRemaining() const { return capacity_.to_bytes(); }

}  // namespace feedback
