// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZBI_BOOTFS_ZBI_BOOTFS_H_
#define ZBI_BOOTFS_ZBI_BOOTFS_H_

#include <fuchsia/hardware/skipblock/c/fidl.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/image.h>
#include <zircon/types.h>

namespace zbi_bootfs {

struct Entry {
  uint64_t size;
  zx::vmo vmo;
};

// This library loads a compressed ZBI Image from "input"
// decompresses it and enable parsing files in its payload.
// Only ZBI_TYPE_STORAGE_BOOTFS type payloads are supported currently.
class __EXPORT ZbiBootfsParser {
 public:
  virtual ~ZbiBootfsParser() {}

  // This loads the ZBI image from "input" to a vmo. It takes an optional parameter
  // byte_offset
  // byte_offset = 0 implies that bytes will be read without any offset
  // relative to the start of the file/partition
  virtual zx_status_t Init(const char* input, size_t byte_offset = 0);

  // This parses the VMO for "filename" and writes its contents to "vmo_out"
  virtual zx_status_t ProcessZbi(const char* filename, Entry* entry);

 protected:
  zx_status_t LoadZbi(const char* input, size_t byte_offset);

 private:
  bool IsSkipBlock(const char* path, fuchsia_hardware_skipblock_PartitionInfo* partition_info);
  zx_status_t FindBootZbi(uint32_t* read_offset, zbi_header_t* header);

  zx::vmo zbi_vmo_;
};

__EXPORT
zx_status_t Decompress(zx::vmo& input, uint64_t input_offset, size_t input_size, zx::vmo& output,
                       uint64_t output_offset, size_t output_size);

}  // namespace zbi_bootfs

#endif  // ZBI_BOOTFS_ZBI_BOOTFS_H_
