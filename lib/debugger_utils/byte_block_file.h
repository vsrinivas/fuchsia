// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "byte_block.h"

namespace debugserver {

// The API for accessing a file via the "byte block" interface.
// This is a fixed size block of contiguous bytes.

class FileByteBlock final : public ByteBlock {
 public:
  // This consumes fd;
  explicit FileByteBlock(int fd);
  ~FileByteBlock();

  bool Read(uintptr_t address, void* out_buffer, size_t length) const override;
  bool Write(uintptr_t address, const void* buffer,
             size_t length) const override;

 private:
  int fd_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FileByteBlock);
};

}  // namespace debugserver
