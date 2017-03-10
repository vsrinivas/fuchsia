// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "memory.h"

namespace debugserver {
namespace util {

// The API for accessing a file via the "memory" interface.
// TODO(dje): Find a better term than "memory".
// This is a fixed size block of contiguous bytes.

class FileMemory final : public util::Memory {
 public:
  // This consumes fd;
  explicit FileMemory(int fd);
  ~FileMemory();

  bool Read(uintptr_t address, void* out_buffer, size_t length)
    const override;
  bool Write(uintptr_t address, const void* buffer, size_t length)
    const override;

 private:
  int fd_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FileMemory);
};

}  // namespace util
}  // namespace debugserver
