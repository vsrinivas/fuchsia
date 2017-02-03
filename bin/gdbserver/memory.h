// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lib/ftl/macros.h"

namespace debugserver {
namespace util {

// An API for accessing "memory".
// TODO(dje): Find a better name than "memory".
// This is a fixed size, randomly accessible, block of contiguous bytes.
// No rush though, the name is "good enough" until things settle.

class Memory {
 public:
  virtual ~Memory() = default;

  // Reads the block of memory of length |length| bytes starting at address
  // |address| into |out_buffer|. |out_buffer| must be at least as large as
  // |length|.
  // Returns true on success or false on failure.
  virtual bool Read(uintptr_t address,
                    void* out_buffer,
                    size_t length) const = 0;

  // Writes the block of memory of length |length| bytes from |data| to the
  // memory address |address| of this process.
  // Returns true on success or false on failure.
  virtual bool Write(uintptr_t address,
                     const void* data,
                     size_t length) const = 0;

 protected:
  Memory() = default;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Memory);
};

}  // namespace util
}  // namespace debugserver
