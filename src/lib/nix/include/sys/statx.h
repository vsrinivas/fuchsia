// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_NIX_INCLUDE_SYS_STATX_H_
#define SRC_LIB_NIX_INCLUDE_SYS_STATX_H_

#include <lib/fdio/vfs.h>
#include <stdint.h>
#include <sys/types.h>

#include <bits/alltypes.h>

#ifdef __cplusplus
extern "C" {
#endif

// The `mask` argument is used to indicate which fields the caller is interested in.
// These values are also returned by `stx_mask` in struct statx indicating
// the values that were actually returned or ignored.
// The `mask` argument to statx() and `stx_mask` in struct statx is an ORed combination
// of the following:
#define STATX_TYPE 0x00000001U         // Want stx_mode & S_IFMT
#define STATX_MODE 0x00000002U         // Want/got stx_mode & ~S_IFMT
#define STATX_NLINK 0x00000004U        // Want/got stx_nlink
#define STATX_UID 0x00000008U          // Want/got stx_uid
#define STATX_GID 0x00000010U          // Want/got stx_gid.
#define STATX_ATIME 0x00000020U        // Want/got stx_atime.
#define STATX_MTIME 0x00000040U        // Want/got stx_mtime.
#define STATX_CTIME 0x00000080U        // Want/got stx_ctime.
#define STATX_INO 0x00000100U          // Want/got stx_ino.
#define STATX_SIZE 0x00000200U         // Want/got stx_size.
#define STATX_BLOCKS 0x00000400U       // Want/got stx_blocks.
#define STATX_BASIC_STATS 0x000007ffU  // Same as the stat struct.
#define STATX_BTIME 0x00000800U        // Want/got stx_btime.
#define STATX_MNT_ID 0x00001000U       // Got stx_mnt_id.
#define STATX__RESERVED 0x80000000U    // Reserved for future struct statx expansion.

struct statx_timestamp {
  int64_t tv_sec;    // Seconds since the Epoch (UNIX time).
  uint32_t tv_nsec;  // Nanoseconds since tv_sec.
};

struct statx {
  uint32_t stx_mask;             // Mask of bits indicating filled fields.
  uint32_t stx_blksize;          // Block size for filesystem I/O.
  uint64_t stx_attributes;       // Extra file attribute indicators.
  uint32_t stx_nlink;            // Number of hard links.
  uint32_t stx_uid;              // User ID of owner.
  uint32_t stx_gid;              // Group ID of owner.
  uint16_t stx_mode;             // File type and mode.
  uint64_t stx_ino;              // Inode number.
  uint64_t stx_size;             // Total size in bytes.
  uint64_t stx_blocks;           // Number of 512B blocks allocated.
  uint64_t stx_attributes_mask;  // Mask to show what's supported in stx_attributes.

  // The following fields are file timestamps.
  struct statx_timestamp stx_atime;  // Last access.
  struct statx_timestamp stx_btime;  // Creation.
  struct statx_timestamp stx_ctime;  // Last status change.
  struct statx_timestamp stx_mtime;  // Last modification.

  // If this file represents a device, then the next two
  // fields contain the ID of the device.
  uint32_t stx_rdev_major;  // Major ID.
  uint32_t stx_rdev_minor;  // Minor ID.

  // The next two fields contain the ID of the device
  // containing the filesystem where the file resides.
  uint32_t stx_dev_major;  // Major ID.
  uint32_t stx_dev_minor;  // Minor ID.
};

int statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *statxbuf);

#ifdef __cplusplus
}
#endif

#endif  // SRC_LIB_NIX_INCLUDE_SYS_STATX_H_
