// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/factory/factoryfs/superblock.h"

#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <iomanip>
#include <limits>

#ifdef __Fuchsia__
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#endif

namespace factoryfs {

namespace {

// Dumps the content of superblock.
void DumpSuperblock(const Superblock& info) {
  FX_LOGS(DEBUG) << "magic:  " << std::setw(10) << info.magic;
  FX_LOGS(DEBUG) << "major version:  " << std::setw(10) << info.major_version;
  FX_LOGS(DEBUG) << "minor version:  " << std::setw(10) << info.minor_version;
  FX_LOGS(DEBUG) << "flags:  " << std::setw(10) << info.flags;
  FX_LOGS(DEBUG) << "data blocks:  " << std::setw(10) << info.data_blocks;
  FX_LOGS(DEBUG) << "directory size:  " << std::setw(10) << info.directory_size;
  FX_LOGS(DEBUG) << "directory entries:  " << std::setw(10) << info.directory_entries;
  FX_LOGS(DEBUG) << "block size  @ " << std::setw(10) << info.block_size;
  FX_LOGS(DEBUG) << "num directory entry blocks  " << std::setw(10) << info.directory_ent_blocks;
  FX_LOGS(DEBUG) << "directory entry start block @ " << std::setw(10)
                 << info.directory_ent_start_block;
}

}  // namespace

// Validate the metadata for the superblock
zx_status_t CheckSuperblock(const Superblock* info) {
  if (info->magic != kFactoryfsMagic) {
    FX_LOGS(ERROR) << "bad magic";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  if (info->major_version != kFactoryfsMajorVersion) {
    FX_LOGS(ERROR) << "FS Version: " << std::setfill('0') << std::setw(8) << std::hex
                   << info->major_version << ". Driver version: " << std::setw(8)
                   << kFactoryfsMajorVersion;
    DumpSuperblock(*info);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (info->major_version != kFactoryfsMajorVersion) {
    FX_LOGS(ERROR) << "FS Major Version: " << std::setfill('0') << std::setw(8) << std::hex
                   << info->major_version << ". Driver version: " << std::setw(8)
                   << kFactoryfsMajorVersion;
    DumpSuperblock(*info);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (info->minor_version != kFactoryfsMinorVersion) {
    FX_LOGS(ERROR) << "FS Minor Version: " << std::setfill('0') << std::setw(8) << std::hex
                   << info->major_version << ". Driver version: " << std::setw(8)
                   << kFactoryfsMinorVersion;
    DumpSuperblock(*info);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (info->block_size != kFactoryfsBlockSize) {
    FX_LOGS(ERROR) << "bsz " << info->block_size << " unsupported";
    DumpSuperblock(*info);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Check if flags (currently unused) are zeroed out correctly.
  if (info->flags != 0) {
    FX_LOGS(ERROR) << "flags set to incorrect value: " << std::setfill('0') << std::setw(8)
                   << std::hex << info->flags;
    DumpSuperblock(*info);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  for (uint32_t i = 0; i < kFactoryfsReserved; i++) {
    if (info->reserved[i]) {
      FX_LOGS(ERROR) << "reserved bits are not zeroed out correctly";
      DumpSuperblock(*info);
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
  }

  DumpSuperblock(*info);
  FX_LOGS(DEBUG) << "Checksuperblock success";
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
