// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the on-disk format of MinFS

#ifndef SRC_STORAGE_MINFS_FORMAT_H_
#define SRC_STORAGE_MINFS_FORMAT_H_

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <limits>

#include <fbl/algorithm.h>

namespace minfs {

// Type of a reference to block number, either absolute (able to index into disk directly) or
// relative to some entity (such as a file).
using blk_t = uint32_t;

// The type of an inode number, which may be used as an index into the inode table.
using ino_t = uint32_t;

// clang-format off

constexpr uint64_t kMinfsMagic0         = (0x002153466e694d21ULL);
constexpr uint64_t kMinfsMagic1         = (0x385000d3d3d3d304ULL);

// Increment the FormatRevision for each backward-incompatible format change, and increment
// kMinfsRevison for every change to how things are serialized, whether or not they are backwards
// compatible. See //src/storage/docs/versioning.md
constexpr uint32_t kMinfsCurrentFormatVersion = 9u;
// Revision 2: Removed minor_version field.
constexpr uint32_t kMinfsCurrentRevision      = 2u;

constexpr ino_t    kMinfsRootIno        = 1;
constexpr uint32_t kMinfsFlagClean      = 0x00000001;  // Currently unused,
constexpr uint32_t kMinfsFlagFVM        = 0x00000002;  // Mounted on FVM.
constexpr uint32_t kMinfsBlockSize      = 8192;
constexpr uint32_t kMinfsBlockBits      = (kMinfsBlockSize * 8);
constexpr uint32_t kMinfsInodeSize      = 256;
constexpr uint32_t kMinfsInodesPerBlock = (kMinfsBlockSize / kMinfsInodeSize);

constexpr uint32_t kMinfsDirect         = 16;
constexpr uint32_t kMinfsIndirect       = 31;
constexpr uint32_t kMinfsDoublyIndirect = 1;

constexpr uint32_t kMinfsDirectPerIndirect  = (kMinfsBlockSize / sizeof(blk_t));
constexpr uint32_t kMinfsDirectPerDindirect = kMinfsDirectPerIndirect * kMinfsDirectPerIndirect;

// It is not possible to have a block at or past this one due to the limitations of the inode and
// indirect blocks.
// TODO(fxbug.dev/31412): Remove this artificial cap when MinFS can safely deal with files larger than 4GB.
constexpr uint64_t kMinfsMaxFileBlock =
    (std::numeric_limits<uint32_t>::max() / kMinfsBlockSize) - 1;

constexpr uint64_t kMinfsMaxFileSize = kMinfsMaxFileBlock * kMinfsBlockSize;

constexpr uint32_t kMinfsTypeFile = 8;
constexpr uint32_t kMinfsTypeDir  = 4;

// Number of blocks allocated to the superblock.
constexpr blk_t kSuperblockBlocks = 1;

// Number of blocks allocated to the backup superblock.
constexpr blk_t kBackupSuperblockBlocks = 1;

// Superblock location.
constexpr size_t kSuperblockStart = 0;

// NonFVM and FVM backup superblock locations.
constexpr size_t kNonFvmSuperblockBackup = 7;
constexpr size_t kFvmSuperblockBackup    = 0x40000;

constexpr size_t kFVMBlockInodeBmStart = 0x10000;
constexpr size_t kFVMBlockDataBmStart  = 0x20000;
constexpr size_t kFVMBlockInodeStart   = 0x30000;
constexpr size_t kFVMBlockJournalStart = kFvmSuperblockBackup + kBackupSuperblockBlocks;
constexpr size_t kFVMBlockDataStart    = 0x50000;

constexpr blk_t kJournalEntryHeaderMaxBlocks = 2040;

// clang-format on

constexpr uint32_t MinfsMagic(uint32_t T) { return 0xAA6f6e00 | T; }
constexpr uint32_t kMinfsMagicDir = MinfsMagic(kMinfsTypeDir);
constexpr uint32_t kMinfsMagicFile = MinfsMagic(kMinfsTypeFile);
constexpr uint32_t MinfsMagicType(uint32_t n) { return n & 0xFF; }
constexpr uint32_t kMinfsMagicPurged = 0xdeaddead;

struct Superblock {
  uint64_t magic0;
  uint64_t magic1;

  // The format version is the version of the overall format. If this is larger than
  // kCurrentMinfsFormatVersion the driver must not access the data.
  //
  // See also "oldest_revision" below and //src/storage/docs/versioning.md.
  //
  // The deprecated2 field used to store a minor version which was never used and should always
  // be zero. Old versions of the MinFS driver will fail to mount if this field is nonzero when
  // otherwise they may have been able to mount a filesystem of version 9.
  uint32_t format_version;
  uint32_t deprecated2;

  uint32_t checksum;          // Crc32 checksum of the contents of the info block.
  uint32_t generation_count;  // Generation count of backup superblock for debugging purpose.
  uint32_t flags;
  uint32_t block_size;             // 8K typical.
  uint32_t inode_size;             // 256.
  uint32_t block_count;            // total number of data blocks.
  uint32_t inode_count;            // total number of inodes.
  uint32_t alloc_block_count;      // total number of allocated data blocks.
  uint32_t alloc_inode_count;      // total number of allocated inodes.
  uint32_t ibm_block;              // first blockno of inode allocation bitmap.
  uint32_t abm_block;              // first blockno of block allocation bitmap.
  uint32_t ino_block;              // first blockno of inode table.
  uint32_t integrity_start_block;  // first blockno available for journal + backup superblock.
  uint32_t dat_block;              // first blockno available for file data.

  // The following fields are only valid with (flags & kMinfsFlagFVM):
  uint32_t slice_size;        // Underlying slice size.
  uint32_t deprecated1;       // Unused but not necessarily 0 (saved total vslices in old vers.).
  uint32_t ibm_slices;        // Slices allocated to inode bitmap.
  uint32_t abm_slices;        // Slices allocated to block bitmap.
  uint32_t ino_slices;        // Slices allocated to inode table.
  uint32_t integrity_slices;  // Slices allocated to integrity section (journal + backup
                              // superblock).
  uint32_t dat_slices;        // Slices allocated to file data section.

  uint32_t unlinked_head;  // Index to the first unlinked (but open) inode.
  uint32_t unlinked_tail;  // Index to the last unlinked (but open) inode.

  // Records the oldest revision of Minfs code that has touched this volume. It can be used for
  // example by fsck to determine what checks should be strict and what should be warnings. This
  // should be incremented any time there's any change in how data is written to the device, even
  // if it's backwards compatible. Compatibility is determined by format_version above.
  //
  // See //src/storage/docs/versioning.md
  uint32_t oldest_revision;

  uint32_t reserved[2018];

  uint32_t BlockSize() const {
    // Either intentionally or unintenttionally, we do not want to change block
    // size to anything other than kMinfsBlockSize yet. This is because changing
    // block size might lead to format change and also because anything other
    // than 8k is not well tested. So assert when we find block size other
    // than 8k.
    ZX_ASSERT(block_size == kMinfsBlockSize);
    return block_size;
  }

  // Returns true if kMinfsFlagFVM is set for superblock.
  bool GetFlagFvm() const { return (flags & kMinfsFlagFVM) == kMinfsFlagFVM; }

  // Returns first block number from where inode bitmap starts.
  uint64_t InodeBitmapStartBlock() const {
    if (!GetFlagFvm()) {
      return ibm_block;
    }
    return kFVMBlockInodeBmStart;
  }

  // Returns first block number from where data bitmap starts.
  uint64_t DataBitmapStartBlock() const {
    if (!GetFlagFvm()) {
      return abm_block;
    }
    return kFVMBlockDataBmStart;
  }

  // Returns first block number from where inode table starts.
  uint64_t InodeTableStartBlock() const {
    if (!GetFlagFvm()) {
      return ino_block;
    }
    return kFVMBlockInodeStart;
  }

  // Returns first block number from where data blocks starts.
  uint64_t DataStartBlock() const {
    if (!GetFlagFvm()) {
      return dat_block;
    }
    return kFVMBlockDataStart;
  }

  // Returns first block number from where backup superblock starts.
  uint64_t BackupSuperblockStart() const {
    if (!GetFlagFvm()) {
      return kNonFvmSuperblockBackup;
    }
    return kFvmSuperblockBackup;
  }
};

static_assert(sizeof(Superblock) == kMinfsBlockSize, "minfs info size is wrong");
// Notes:
// - The inode bitmap, block bitmap, inode table, journal, and data
//   regions must be in that order and may not overlap.
// - The abm has an entry for every block on the volume, including
//   the info block (0), the bitmaps, etc.
// - Data blocks referenced from direct and indirect block tables
//   in inodes are also relative to (0), but it is not legal for
//   a block number of less than dat_block (start of data blocks)
//   to be used.
// - Inode numbers refer to the inode in block:
//     ino_block + ino / kMinfsInodesPerBlock
//   at offset: ino % kMinfsInodesPerBlock.
// - Inode 0 is never used, should be marked allocated but ignored.

// The minimal number of slices to allocate a MinFS partition:
// Superblock, Inode bitmap, Data bitmap, Inode Table, Journal (2), and actual data.
constexpr size_t kMinfsMinimumSlices = 7;

constexpr uint64_t kMinfsDefaultInodeCount = 4096;

struct Inode {
  uint32_t magic;
  uint32_t size;
  uint32_t block_count;
  uint32_t link_count;
  uint64_t create_time;
  uint64_t modify_time;
  uint32_t seq_num;       // bumped when modified
  uint32_t gen_num;       // bumped when deleted
  uint32_t dirent_count;  // for directories
  ino_t last_inode;       // index to the previous unlinked inode
  ino_t next_inode;       // index to the next unlinked inode
  uint32_t rsvd[3];
  blk_t dnum[kMinfsDirect];           // direct blocks
  blk_t inum[kMinfsIndirect];         // indirect blocks
  blk_t dinum[kMinfsDoublyIndirect];  // doubly indirect blocks
};

static_assert(sizeof(Inode) == kMinfsInodeSize, "minfs inode size is wrong");

struct Dirent {
  ino_t ino;        // Inode number.
  uint32_t reclen;  // Low 28 bits: Length of record. High 4 bits: Flags
  uint8_t namelen;  // Length of the filename.
  uint8_t type;     // One of kMinfsType*.
  char name[];      // The name bytes follow immediately. There is no trailing null.
};

constexpr uint8_t kMinfsDirentAlignment = 4;
constexpr uint8_t kMinfsDirentAlignmentMask = kMinfsDirentAlignment - 1;
static_assert(kMinfsDirentAlignment == alignof(Dirent), "Dirent alignment changed");

constexpr uint32_t kMinfsDirentSize = sizeof(Dirent);

// Returns the length of the Dirent structure required to hold a name of the given length.
constexpr uint32_t DirentSize(uint8_t namelen) {
  return kMinfsDirentSize + fbl::round_up<uint32_t>(namelen, kMinfsDirentAlignment);
}

constexpr uint8_t kMinfsMaxNameSize = 255;

// The largest acceptable value of DirentSize(dirent->namelen). The 'dirent->reclen' field may be
// larger after coalescing entries.
constexpr uint32_t kMinfsMaxDirentSize = DirentSize(kMinfsMaxNameSize);
constexpr uint32_t kMinfsMaxDirectorySize = fbl::round_down((1u << 20) - 1, kMinfsDirentAlignment);
static_assert(kMinfsMaxDirectorySize % kMinfsDirentAlignment == 0, "Invalid max directory size");
static_assert(kMinfsMaxDirectorySize < (1u << 20), "Max directory size too large");

// Storage for a Dirent padded out to the size for the maximum length. This is used as a buffer to
// read into with the correct alignment.
template <size_t max_size = kMinfsMaxDirentSize>
union DirentBuffer {
  uint8_t raw[max_size];
  Dirent dirent;
};

static_assert(kMinfsMaxNameSize >= NAME_MAX,
              "MinFS names must be large enough to hold NAME_MAX characters");

constexpr uint32_t kMinfsReclenMask = 0x0FFFFFFF;
constexpr uint32_t kMinfsReclenLast = 0x80000000;

constexpr uint32_t MinfsReclen(Dirent* de, size_t off) {
  return (de->reclen & kMinfsReclenLast) ? kMinfsMaxDirectorySize - static_cast<uint32_t>(off)
                                         : de->reclen & kMinfsReclenMask;
}

static_assert(kMinfsMaxDirectorySize <= kMinfsReclenMask,
              "MinFS directory size must be smaller than reclen mask");

// Notes:
// - dirents with ino of 0 are free, and skipped over on lookup.
// - reclen must be a multiple of 4.
// - The last record in a directory has the "kMinfsReclenLast" flag set. The actual size of this
//   record can be computed from the offset at which this record starts. If the MAX_DIR_SIZE is
//   increased, this 'last' record will also increase in size.

// blocksize   8K    16K    32K
// 16 dir =  128K   256K   512K
// 32 ind =  512M  1024M  2048M

//  1GB ->  128K blocks ->  16K bitmap (2K qword)
//  4GB ->  512K blocks ->  64K bitmap (8K qword)
// 32GB -> 4096K blocks -> 512K bitmap (64K qwords)

// Block Cache (bcache.c).
constexpr uint32_t kMinfsHashBits = (8);

// Sets kMinfsFlagFVM for given superblock.
constexpr void SetMinfsFlagFvm(Superblock& info) { info.flags |= kMinfsFlagFVM; }

constexpr uint64_t InodeBitmapBlocks(const Superblock& info) {
  if ((info.flags & kMinfsFlagFVM) == kMinfsFlagFVM) {
    auto blocks_per_slice = static_cast<blk_t>(info.slice_size / kMinfsBlockSize);
    return info.ibm_slices * blocks_per_slice;
  }

  return info.abm_block - info.ibm_block;
}

constexpr uint64_t BlockBitmapBlocks(const Superblock& info) {
  if ((info.flags & kMinfsFlagFVM) == kMinfsFlagFVM) {
    auto blocks_per_slice = static_cast<blk_t>(info.slice_size / kMinfsBlockSize);
    return info.abm_slices * blocks_per_slice;
  }

  return info.ino_block - info.abm_block;
}

constexpr uint64_t InodeBlocks(const Superblock& info) {
  if ((info.flags & kMinfsFlagFVM) == kMinfsFlagFVM) {
    auto blocks_per_slice = static_cast<blk_t>(info.slice_size / kMinfsBlockSize);
    return info.ino_slices * blocks_per_slice;
  }

  return info.integrity_start_block - info.ino_block;
}

constexpr uint64_t JournalStartBlock(const Superblock& info) {
  if ((info.flags & kMinfsFlagFVM) == kMinfsFlagFVM) {
    return kFVMBlockJournalStart;
  }

  return info.integrity_start_block + kBackupSuperblockBlocks;
}

constexpr uint64_t JournalBlocks(const Superblock& info) {
  if ((info.flags & kMinfsFlagFVM) == kMinfsFlagFVM) {
    auto blocks_per_slice = static_cast<blk_t>(info.slice_size / kMinfsBlockSize);
    return info.integrity_slices * blocks_per_slice - kBackupSuperblockBlocks;
  }

  return info.dat_block - info.integrity_start_block - kBackupSuperblockBlocks;
}

constexpr uint64_t DataBlocks(const Superblock& info) {
  if ((info.flags & kMinfsFlagFVM) == kMinfsFlagFVM) {
    auto blocks_per_slice = static_cast<blk_t>(info.slice_size / kMinfsBlockSize);
    return info.dat_slices * blocks_per_slice;
  }

  return info.block_count;
}

constexpr uint64_t NonDataBlocks(const Superblock& info) {
  return InodeBitmapBlocks(info) + BlockBitmapBlocks(info) + InodeBlocks(info) +
         JournalBlocks(info);
}

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_FORMAT_H_
