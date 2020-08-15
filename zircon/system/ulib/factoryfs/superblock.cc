// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <limits>

#include <factoryfs/superblock.h>
#include <fs/trace.h>

#ifdef __Fuchsia__
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#endif

namespace factoryfs {

namespace {
// Dumps the content of superblock to |out|. Does nothing if |out| is nullptr.
void DumpSuperblock(const Superblock& info, FILE* out) {
  if (out == nullptr) {
    return;
  }

  fprintf(out,
          "info.magic: %" PRIu64
          "\n"
          "info.major_version: %" PRIu32
          "\n"
          "info.minor_version: %" PRIu32
          "\n"
          "info.flags: %" PRIu32
          "\n"
          "info.data_blocks: %" PRIu32
          "\n"
          "info.directory_size: %" PRIu32
          "\n"
          "info.directory_entries: %" PRIu32
          "\n"
          "info.block_size: %" PRIu32
          "\n"
          "info.directory_ent_blocks: %" PRIu32
          "\n"
          "info.directory_ent_start_block: %" PRIu32 "\n",
          info.magic, info.major_version, info.minor_version, info.flags, info.data_blocks,
          info.directory_size, info.directory_entries, info.block_size, info.directory_ent_blocks,
          info.directory_ent_start_block);
}

}  // namespace

// Validate the metadata for the superblock
zx_status_t CheckSuperblock(const Superblock* info) {
  if (info->magic != kFactoryfsMagic) {
    FS_TRACE_ERROR("factoryfs: bad magic\n");
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  if (info->major_version != kFactoryfsMajorVersion) {
    FS_TRACE_ERROR("factoryfs: FS Version: %08x. Driver version: %08x\n", info->major_version,
                   kFactoryfsMajorVersion);
    DumpSuperblock(*info, stderr);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (info->major_version != kFactoryfsMajorVersion) {
    FS_TRACE_ERROR("factoryfs: FS Major Version: %08x. Driver version: %08x\n", info->major_version,
                   kFactoryfsMajorVersion);
    DumpSuperblock(*info, stderr);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (info->minor_version != kFactoryfsMinorVersion) {
    FS_TRACE_ERROR("factoryfs: FS Minor Version: %08x. Driver version: %08x\n", info->major_version,
                   kFactoryfsMinorVersion);
    DumpSuperblock(*info, stderr);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (info->block_size != kFactoryfsBlockSize) {
    FS_TRACE_ERROR("factoryfs: bsz %u unsupported\n", info->block_size);
    DumpSuperblock(*info, stderr);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  DumpSuperblock(*info, stderr);
  FS_TRACE_INFO("Checksuperblock success\n");
  return ZX_OK;
}

void InitializeSuperblock(uint64_t block_count, Superblock* info) {
  memset(info, 0x00, sizeof(*info));
  info->magic = kFactoryfsMagic;
  info->major_version = kFactoryfsMajorVersion;
  info->minor_version = kFactoryfsMinorVersion;
  info->flags = 0;
  info->block_size = kFactoryfsBlockSize;
  info->data_blocks = 1;
  info->directory_ent_blocks = 1;
  info->directory_ent_start_block = kDirenStartBlock;
  info->directory_entries = 1;
  info->directory_size = info->directory_ent_blocks * info->directory_entries * kFactoryfsBlockSize;
}

}  // namespace factoryfs
