// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_BOOT_BOOTFS_H_
#define SYSROOT_ZIRCON_BOOT_BOOTFS_H_

#include <stdint.h>

// The payload (after decompression) of an item in BOOTFS format consists
// of separate "file" images that are each aligned to ZBI_BOOTFS_PAGE_SIZE
// bytes from the beginning of the item payload.  The first "file" consists
// of a zbi_bootfs_header_t followed by directory entries.
#define ZBI_BOOTFS_PAGE_SIZE (4096u)

#define ZBI_BOOTFS_PAGE_ALIGN(size) \
  (((size) + ZBI_BOOTFS_PAGE_SIZE - 1) & ~(ZBI_BOOTFS_PAGE_SIZE - 1))

typedef struct {
  // Must be ZBI_BOOTFS_MAGIC.
  uint32_t magic;

  // Size in bytes of all the directory entries.
  // Does not include the size of the zbi_bootfs_header_t.
  uint32_t dirsize;

  // Reserved for future use.  Set to 0.
  uint32_t reserved0;
  uint32_t reserved1;
} zbi_bootfs_header_t;

// LSW of sha256("bootfs")
#define ZBI_BOOTFS_MAGIC (0xa56d3ff9)

// Each directory entry holds a pathname and gives the offset and size
// of the contents of the file by that name.
typedef struct {
  // Length of the name[] field at the end.  This length includes the
  // NUL terminator, which must be present, but does not include any
  // alignment padding required before the next directory entry.
  uint32_t name_len;

  // Length of the file in bytes.  This is an exact size that is not
  // rounded, though the file is always padded with zeros up to a
  // multiple of ZBI_BOOTFS_PAGE_SIZE.
  uint32_t data_len;

  // Offset from the beginning of the payload (zbi_bootfs_header_t) to
  // the file's data.  This must be a multiple of ZBI_BOOTFS_PAGE_SIZE.
  uint32_t data_off;

  // Pathname of the file, a UTF-8 string.  This must include a NUL
  // terminator at the end.  It must not begin with a '/', but it may
  // contain '/' separators for subdirectories.
  char name[];
} zbi_bootfs_dirent_t;

// Each directory entry has a variable size of [16,268] bytes that
// must be a multiple of 4 bytes.
#define ZBI_BOOTFS_DIRENT_SIZE(name_len) \
  ((sizeof(zbi_bootfs_dirent_t) + (name_len) + 3) & -(size_t)4)

// zbi_bootfs_dirent_t.name_len must be > 1 and <= ZBI_BOOTFS_MAX_NAME_LEN.
#define ZBI_BOOTFS_MAX_NAME_LEN (256)

#endif  // SYSROOT_ZIRCON_BOOT_BOOTFS_H_
