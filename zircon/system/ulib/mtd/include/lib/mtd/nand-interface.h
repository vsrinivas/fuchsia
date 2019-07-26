// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/types.h>

namespace mtd {

// Base interface for a NAND-based storage device.
class NandInterface {
 public:
  virtual ~NandInterface() {}

  // Gets the page size in bytes.
  virtual uint32_t PageSize() = 0;
  // Gets the block size in bytes.
  virtual uint32_t BlockSize() = 0;
  // Gets the out-of-band (aka spare or OOB) size in bytes.
  virtual uint32_t OobSize() = 0;
  // Gets the size of the NAND-based storage device in bytes. This value may not
  // represent the full size of the chip depending on the implementation of
  // this interface.
  virtual uint32_t Size() = 0;

  // Reads the OOB at the specified |byte_offset| into the buffer specified by
  // |bytes|. |byte_offset| should be a multiple of |PageSize|. |bytes| should
  // be at least |OobSize| large. Returns ZX_OK on success.
  virtual zx_status_t ReadOob(uint32_t byte_offset, void* bytes) = 0;

  // Reads the page at the specified |byte_offset| into the buffer specified by
  // |bytes|. Actual number of bytes read is stored in |actual|. |byte_offset|
  // should be a multiple of |PageSize|. |bytes| should be at least |PageSize|
  // bytes large. Returns ZX_OK on success.
  virtual zx_status_t ReadPage(uint32_t byte_offset, void* bytes, uint32_t* actual) = 0;

  // Writes the |data| and |oob| buffers to the page specified at |byte_offset|.
  // Both buffers are required and are expected to be |PageSize| and |OobSize|
  // bytes, respectively. |byte_offset| should be a multiple of page size.
  // Returns ZX_OK on success.
  virtual zx_status_t WritePage(uint32_t byte_offset, const void* data, const void* oob) = 0;

  // Erases the block at |byte_offset|. |byte_offset| should be a multiple of
  // |BlockSize|. Returns ZX_OK on success.
  virtual zx_status_t EraseBlock(uint32_t byte_offset) = 0;

  // Determines if the block at |byte_offset| is marked bad. On success,
  // |is_bad_block| contains the bad block status. Returns ZX_OK on success.
  virtual zx_status_t IsBadBlock(uint32_t byte_offset, bool* is_bad_block) = 0;
};

}  // namespace mtd
