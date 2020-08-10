// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_FORMAT_ASSERTIONS_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_FORMAT_ASSERTIONS_

#include <cstddef>

#include <blobfs/format.h>

// This file tests the on-disk structures of Blobfs.

namespace blobfs {

// clang-format off

#define PADDING_LENGTH(type, prev, next)                                                 \
    (offsetof(type, next) - (offsetof(type, prev) + sizeof(type{}.prev)))

// Ensure that the members don't change their offsets within the structure
static_assert(offsetof(Superblock, magic0) ==                           0x0);
static_assert(offsetof(Superblock, magic1) ==                           0x8);
static_assert(offsetof(Superblock, version) ==                          0x10);
static_assert(offsetof(Superblock, flags) ==                            0x14);
static_assert(offsetof(Superblock, block_size) ==                       0x18);
static_assert(offsetof(Superblock, data_block_count) ==                 0x20);
static_assert(offsetof(Superblock, journal_block_count) ==              0x28);
static_assert(offsetof(Superblock, inode_count) ==                      0x30);
static_assert(offsetof(Superblock, alloc_block_count) ==                0x38);
static_assert(offsetof(Superblock, alloc_inode_count) ==                0x40);
static_assert(offsetof(Superblock, reserved2) ==                        0x48);
static_assert(offsetof(Superblock, slice_size) ==                       0x50);
static_assert(offsetof(Superblock, vslice_count) ==                     0x58);
static_assert(offsetof(Superblock, abm_slices) ==                       0x60);
static_assert(offsetof(Superblock, ino_slices) ==                       0x64);
static_assert(offsetof(Superblock, dat_slices) ==                       0x68);
static_assert(offsetof(Superblock, journal_slices) ==                   0x6c);

// Ensure that the padding between two members doesn't change
static_assert(PADDING_LENGTH(Superblock, magic0,                 magic1) ==                 0);
static_assert(PADDING_LENGTH(Superblock, magic1,                 version) ==                0);
static_assert(PADDING_LENGTH(Superblock, version,                flags) ==                  0);
static_assert(PADDING_LENGTH(Superblock, flags,                  block_size) ==             0);
static_assert(PADDING_LENGTH(Superblock, block_size,             data_block_count) ==       4);
static_assert(PADDING_LENGTH(Superblock, data_block_count,       journal_block_count) ==    0);
static_assert(PADDING_LENGTH(Superblock, journal_block_count,    inode_count) ==            0);
static_assert(PADDING_LENGTH(Superblock, inode_count,            alloc_block_count) ==      0);
static_assert(PADDING_LENGTH(Superblock, alloc_block_count,      alloc_inode_count) ==      0);
static_assert(PADDING_LENGTH(Superblock, alloc_inode_count,      reserved2) ==              0);
static_assert(PADDING_LENGTH(Superblock, reserved2,              slice_size) ==             0);
static_assert(PADDING_LENGTH(Superblock, slice_size,             vslice_count) ==           0);
static_assert(PADDING_LENGTH(Superblock, vslice_count,           abm_slices) ==             0);
static_assert(PADDING_LENGTH(Superblock, abm_slices,             ino_slices) ==             0);
static_assert(PADDING_LENGTH(Superblock, ino_slices,             dat_slices) ==             0);
static_assert(PADDING_LENGTH(Superblock, dat_slices,             journal_slices) ==         0);
static_assert(PADDING_LENGTH(Superblock, journal_slices,         reserved) ==               0);

// Ensure that the padding at the end of structure doesn't change
static_assert(sizeof(Superblock) ==
              offsetof(Superblock, reserved) + sizeof(Superblock{}.reserved));


// Ensure that the members don't change their offsets within the structure
static_assert(offsetof(NodePrelude, flags) ==               0x0);
static_assert(offsetof(NodePrelude, version) ==             0x02);
static_assert(offsetof(NodePrelude, next_node) ==           0x4);

// Ensure that the padding between two members doesn't change
static_assert(PADDING_LENGTH(NodePrelude, flags,        version) ==         0);
static_assert(PADDING_LENGTH(NodePrelude, version,      next_node) ==       0);

// Ensure that the padding at the end of structure doesn't change
static_assert(sizeof(NodePrelude) ==
              offsetof(NodePrelude, next_node) + sizeof(NodePrelude{}.next_node));

// Ensure that the members don't change their offsets within the structure
static_assert(offsetof(Inode, header) == 0x00);
static_assert(offsetof(Inode, merkle_root_hash) == 0x08);
static_assert(offsetof(Inode, blob_size) == 0x28);
static_assert(offsetof(Inode, block_count) == 0x30);
static_assert(offsetof(Inode, extent_count) == 0x34);
static_assert(offsetof(Inode, reserved) == 0x36);
static_assert(offsetof(Inode, extents) == 0x38);

// Ensure that the padding between two members doesn't change
static_assert(PADDING_LENGTH(Inode, header,                     merkle_root_hash) == 0);
static_assert(PADDING_LENGTH(Inode, merkle_root_hash,           blob_size) == 0);
static_assert(PADDING_LENGTH(Inode, blob_size,                  block_count) == 0);
static_assert(PADDING_LENGTH(Inode, block_count,                extent_count) == 0);
static_assert(PADDING_LENGTH(Inode, extent_count,               reserved) == 0);
static_assert(PADDING_LENGTH(Inode, reserved,                   extents) == 0);

// Ensure that the padding at the end of structure doesn't change
static_assert(sizeof(Inode) == offsetof(Inode, extents) + sizeof(Inode{}.extents));

// Ensure that the members don't change their offsets within the structure
static_assert(offsetof(ExtentContainer, header) == 0x00);
static_assert(offsetof(ExtentContainer, previous_node) == 0x08);
static_assert(offsetof(ExtentContainer, extent_count) == 0x0c);
static_assert(offsetof(ExtentContainer, reserved) == 0x0e);
static_assert(offsetof(ExtentContainer, extents) == 0x10);

// Ensure that the padding between two members doesn't change
static_assert(PADDING_LENGTH(ExtentContainer, header,           previous_node) == 0);
static_assert(PADDING_LENGTH(ExtentContainer, previous_node,    extent_count) == 0);
static_assert(PADDING_LENGTH(ExtentContainer, extent_count,     reserved) == 0);
static_assert(PADDING_LENGTH(ExtentContainer, reserved,         extents) == 0);

// Ensure that the padding at the end of structure doesn't change
static_assert(sizeof(ExtentContainer) ==
              offsetof(ExtentContainer, extents) + sizeof(ExtentContainer{}.extents));

// clang-format on

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_FORMAT_ASSERTIONS_
