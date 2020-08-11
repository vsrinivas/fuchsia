// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_MANAGEMENT_FORMAT_H_
#define FS_MANAGEMENT_FORMAT_H_

#include <zircon/types.h>

__BEGIN_CDECLS

typedef enum disk_format_type {
  DISK_FORMAT_UNKNOWN,
  DISK_FORMAT_GPT,
  DISK_FORMAT_MBR,
  DISK_FORMAT_MINFS,
  DISK_FORMAT_FAT,
  DISK_FORMAT_BLOBFS,
  DISK_FORMAT_FVM,
  DISK_FORMAT_ZXCRYPT,
  DISK_FORMAT_BLOCK_VERITY,
  DISK_FORMAT_COUNT_,
} disk_format_t;

static const char* disk_format_string_[DISK_FORMAT_COUNT_] = {
    [DISK_FORMAT_UNKNOWN] = "unknown", [DISK_FORMAT_GPT] = "gpt",
    [DISK_FORMAT_MBR] = "mbr",         [DISK_FORMAT_MINFS] = "minfs",
    [DISK_FORMAT_FAT] = "fat",         [DISK_FORMAT_BLOBFS] = "blobfs",
    [DISK_FORMAT_FVM] = "fvm",         [DISK_FORMAT_ZXCRYPT] = "zxcrypt"};

static inline const char* disk_format_string(disk_format_t fs_type) {
  return disk_format_string_[fs_type];
}

#define HEADER_SIZE 4096

static const uint8_t minfs_magic[16] = {
    0x21, 0x4d, 0x69, 0x6e, 0x46, 0x53, 0x21, 0x00, 0x04, 0xd3, 0xd3, 0xd3, 0xd3, 0x00, 0x50, 0x38,
};

static const uint8_t blobfs_magic[16] = {
    0x21, 0x4d, 0x69, 0x9e, 0x47, 0x53, 0x21, 0xac, 0x14, 0xd3, 0xd3, 0xd4, 0xd4, 0x00, 0x50, 0x98,
};

static const uint8_t gpt_magic[16] = {
    0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54, 0x00, 0x00, 0x01, 0x00, 0x5c, 0x00, 0x00, 0x00,
};

static const uint8_t fvm_magic[8] = {
    0x46, 0x56, 0x4d, 0x20, 0x50, 0x41, 0x52, 0x54,
};

static const uint8_t zxcrypt_magic[16] = {
    0x5f, 0xe8, 0xf8, 0x00, 0xb3, 0x6d, 0x11, 0xe7, 0x80, 0x7a, 0x78, 0x63, 0x72, 0x79, 0x70, 0x74,
};

static const uint8_t block_verity_magic[16] = {0x62, 0x6c, 0x6f, 0x63, 0x6b, 0x2d, 0x76, 0x65,
                                               0x72, 0x69, 0x74, 0x79, 0x2d, 0x76, 0x31, 0x00};

disk_format_t detect_disk_format(int fd);
disk_format_t detect_disk_format_log_unknown(int fd);

__END_CDECLS

#endif  // FS_MANAGEMENT_FORMAT_H_
