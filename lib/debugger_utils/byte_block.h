// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lib/fxl/macros.h"

namespace debugger_utils {

// An API for accessing memory, files, or anything else that is
// fixed size, randomly accessible, block of contiguous bytes.

class ByteBlock {
 public:
  virtual ~ByteBlock() = default;

  // Reads the block of memory of length |length| bytes starting at address
  // |address| into |out_buffer|. |out_buffer| must be at least as large as
  // |length|.
  // Returns true on success or false on failure.
  virtual bool Read(uintptr_t address, void* out_buffer,
                    size_t length) const = 0;

  // Writes the block of memory of length |length| bytes from |buffer| to the
  // memory address |address| of this process.
  // Returns true on success or false on failure.
  virtual bool Write(uintptr_t address, const void* buffer,
                     size_t length) const = 0;

 protected:
  ByteBlock() = default;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ByteBlock);
};

}  // namespace debugger_utils
