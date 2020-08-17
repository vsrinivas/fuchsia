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

// Dumps the content of superblock.
void DumpSuperblock(const Superblock& info) {
  FS_TRACE_DEBUG("factoryfs: magic:  %10lu\n", info.magic);
  FS_TRACE_DEBUG("factoryfs: major version:  %10u\n", info.major_version);
  FS_TRACE_DEBUG("factoryfs: minor version:  %10u\n", info.minor_version);
  FS_TRACE_DEBUG("factoryfs: flags:  %10u\n", info.flags);
  FS_TRACE_DEBUG("factoryfs: data blocks:  %10u\n", info.data_blocks);
  FS_TRACE_DEBUG("factoryfs: directory size:  %10u\n", info.directory_size);
  FS_TRACE_DEBUG("factoryfs: directory entries:  %10u\n", info.directory_entries);
  FS_TRACE_DEBUG("factoryfs: block size  @ %10u\n", info.info.block_size);
  FS_TRACE_DEBUG("factoryfs: num directory entry blocks  %10u\n", info.directory_ent_blocks);
  FS_TRACE_DEBUG("factoryfs: directory entry start block @ %10u\n", info.directory_ent_start_block);
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
    DumpSuperblock(*info);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (info->major_version != kFactoryfsMajorVersion) {
    FS_TRACE_ERROR("factoryfs: FS Major Version: %08x. Driver version: %08x\n", info->major_version,
                   kFactoryfsMajorVersion);
    DumpSuperblock(*info);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (info->minor_version != kFactoryfsMinorVersion) {
    FS_TRACE_ERROR("factoryfs: FS Minor Version: %08x. Driver version: %08x\n", info->major_version,
                   kFactoryfsMinorVersion);
    DumpSuperblock(*info);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (info->block_size != kFactoryfsBlockSize) {
    FS_TRACE_ERROR("factoryfs: bsz %u unsupported\n", info->block_size);
    DumpSuperblock(*info);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  DumpSuperblock(*info);
  FS_TRACE_DEBUG("Checksuperblock success\n");
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
