// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpt/gpt.h"

#include <assert.h>
#include <errno.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <inttypes.h>
#include <lib/cksum.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>  // for zx_cprng_draw

#include <memory>

#include <fbl/algorithm.h>
#include <fbl/vector.h>
#include <gpt/gpt.h>
#include <gpt/guid.h>
#include <mbr/mbr.h>
#include <range/range.h>

#include "gpt.h"

namespace gpt {

namespace {

using BlockRange = range::Range<uint64_t>;

#define G_PRINTF(f, ...) \
  if (debug_out)         \
    printf((f), ##__VA_ARGS__);

bool debug_out = false;

void print_array(const gpt_partition_t* const partitions[kPartitionCount], int c) {
  char GUID[GPT_GUID_STRLEN];
  char name[GPT_NAME_LEN / 2 + 1];

  for (int i = 0; i < c; ++i) {
    uint8_to_guid_string(GUID, partitions[i]->type);
    memset(name, 0, GPT_NAME_LEN / 2 + 1);
    utf16_to_cstring(name, reinterpret_cast<const uint16_t*>(partitions[i]->name),
                     GPT_NAME_LEN / 2);

    printf("Name: %s \n  Start: %lu -- End: %lu \nType: %s\n", name, partitions[i]->first,
           partitions[i]->last, GUID);
  }
}

// Write a block to device "fd", writing "size" bytes followed by zero-byte padding to the next
// block size.
zx_status_t write_partial_block(int fd, void* data, size_t size, size_t offset, size_t blocksize) {
  // If input block is already rounded to blocksize, just directly write from our buffer.
  if (size % blocksize == 0) {
    ssize_t ret = pwrite(fd, data, size, offset);
    if (ret < 0 || static_cast<size_t>(ret) != size) {
      return ZX_ERR_IO;
    }
    return ZX_OK;
  }

  // Otherwise, pad out data to blocksize.
  size_t new_size = fbl::round_up(size, blocksize);
  std::unique_ptr<uint8_t[]> block(new uint8_t[new_size]);
  memcpy(block.get(), data, size);
  memset(block.get() + size, 0, new_size - size);
  ssize_t ret = pwrite(fd, block.get(), new_size, offset);
  if (ret != static_cast<ssize_t>(new_size)) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

void partition_init(gpt_partition_t* part, const char* name, const uint8_t* type,
                    const uint8_t* guid, uint64_t first, uint64_t last, uint64_t flags) {
  memcpy(part->type, type, sizeof(part->type));
  memcpy(part->guid, guid, sizeof(part->guid));
  part->first = first;
  part->last = last;
  part->flags = flags;
  const size_t num_utf16_bits = sizeof(uint16_t);
  cstring_to_utf16(reinterpret_cast<uint16_t*>(part->name), name,
                   sizeof(part->name) / num_utf16_bits);
}

zx_status_t gpt_sync_current(int fd, uint64_t blocksize, gpt_header_t* header,
                             gpt_partition_t* ptable) {
  // write partition table first
  off_t offset = header->entries * blocksize;
  size_t ptable_size = header->entries_count * header->entries_size;
  zx_status_t status = write_partial_block(fd, ptable, ptable_size, offset, blocksize);
  if (status != ZX_OK) {
    return status;
  }

  // then write the header
  offset = header->current * blocksize;
  return write_partial_block(fd, header, sizeof(*header), offset, blocksize);
}

int compare(const void* ls, const void* rs) {
  const auto* l = *static_cast<gpt_partition_t* const*>(ls);
  const auto* r = *static_cast<gpt_partition_t* const*>(rs);
  if (l == NULL && r == NULL) {
    return 0;
  }

  if (l == NULL) {
    return 1;
  }

  if (r == NULL) {
    return -1;
  }

  if (l->first == r->first) {
    return 0;
  }

  return (l->first < r->first) ? -1 : 1;
}

}  // namespace

__BEGIN_CDECLS

void gpt_set_debug_output_enabled(bool enabled) { debug_out = enabled; }

void gpt_sort_partitions(gpt_partition_t** base, size_t count) {
  qsort(base, count, sizeof(gpt_partition_t*), compare);
}

void cstring_to_utf16(uint16_t* dst, const char* src, size_t maxlen) {
  size_t len = strlen(src);
  if (len > maxlen) {
    len = maxlen;
  }
  for (size_t i = 0; i < len; i++) {
    *dst++ = static_cast<uint16_t>(*src++ & 0x7f);
  }
}

char* utf16_to_cstring(char* dst, const uint16_t* src, size_t len) {
  size_t i = 0;
  char* ptr = dst;
  while (i < len) {
    char c = src[i++] & 0x7f;
    if (!c) {
      continue;
    }
    *ptr++ = c;
  }
  return dst;
}

bool gpt_is_sys_guid(uint8_t* guid, ssize_t len) {
  static const uint8_t sys_guid[GPT_GUID_LEN] = GUID_SYSTEM_VALUE;
  return len == GPT_GUID_LEN && !memcmp(guid, sys_guid, GPT_GUID_LEN);
}

bool gpt_is_data_guid(uint8_t* guid, ssize_t len) {
  static const uint8_t data_guid[GPT_GUID_LEN] = GUID_DATA_VALUE;
  return len == GPT_GUID_LEN && !memcmp(guid, data_guid, GPT_GUID_LEN);
}

bool gpt_is_install_guid(uint8_t* guid, ssize_t len) {
  static const uint8_t install_guid[GPT_GUID_LEN] = GUID_INSTALL_VALUE;
  return len == GPT_GUID_LEN && !memcmp(guid, install_guid, GPT_GUID_LEN);
}

bool gpt_is_efi_guid(uint8_t* guid, ssize_t len) {
  static const uint8_t efi_guid[GPT_GUID_LEN] = GUID_EFI_VALUE;
  return len == GPT_GUID_LEN && !memcmp(guid, efi_guid, GPT_GUID_LEN);
}

bool gpt_is_factory_guid(uint8_t* guid, ssize_t len) {
  static const uint8_t factory_guid[GPT_GUID_LEN] = GPT_FACTORY_TYPE_GUID;
  return len == GPT_GUID_LEN && !memcmp(guid, factory_guid, GPT_GUID_LEN);
}

void uint8_to_guid_string(char* dst, const uint8_t* src) {
  // memcpy'd rather than casting src in-place for alignment (for ubsan).
  guid_t guid;
  memcpy(&guid, src, sizeof(guid_t));
  sprintf(dst, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid.data1, guid.data2,
          guid.data3, guid.data4[0], guid.data4[1], guid.data4[2], guid.data4[3], guid.data4[4],
          guid.data4[5], guid.data4[6], guid.data4[7]);
}

const char* gpt_guid_to_type(const char* guid) { return gpt::KnownGuid::GuidStrToName(guid); }

__END_CDECLS

fit::result<gpt_header_t, zx_status_t> InitializePrimaryHeader(uint64_t block_size,
                                                               uint64_t block_count) {
  gpt_header_t header = {};

  if (block_size < kHeaderSize) {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  if (block_count <= MinimumBlockDeviceSize(block_size).value()) {
    return fit::error(ZX_ERR_BUFFER_TOO_SMALL);
  }

  header.magic = kMagicNumber;
  header.revision = kRevision;
  header.size = kHeaderSize;
  header.current = kPrimaryHeaderStartBlock;

  // backup gpt is in the last block
  header.backup = block_count - 1;

  // First usable block is the block after end of primary copy.
  header.first = kPrimaryHeaderStartBlock + MinimumBlocksPerCopy(block_size).value();

  // Last usable block is the block before beginning of backup entries array.
  header.last = block_count - MinimumBlocksPerCopy(block_size).value() - 1;

  // We have ensured above that there are more blocks than MinimumBlockDeviceSize().
  ZX_DEBUG_ASSERT(header.first <= header.last);

  // generate a guid
  zx_cprng_draw(header.guid, GPT_GUID_LEN);

  // fill in partition table fields in header
  header.entries = kPrimaryEntriesStartBlock;
  header.entries_count = kPartitionCount;
  header.entries_size = kEntrySize;

  // Finally, calculate header checksum
  header.crc32 = crc32(0, reinterpret_cast<const uint8_t*>(&header), kHeaderSize);

  return fit::ok(header);
}

// Returns user friendly error message given `status`.
const char* HeaderStatusToCString(zx_status_t status) {
  switch (status) {
    case ZX_OK:
      return "valid partition";
    case ZX_ERR_BAD_STATE:
      return "bad header magic";
    case ZX_ERR_INVALID_ARGS:
      return "invalid header size";
    case ZX_ERR_IO_DATA_INTEGRITY:
      return "invalid header crc";
    case ZX_ERR_IO_OVERRUN:
      return "too many partitions";
    case ZX_ERR_FILE_BIG:
      return "invalid entry size";
    case ZX_ERR_BUFFER_TOO_SMALL:
      return "last block > block count";
    case ZX_ERR_OUT_OF_RANGE:
      return "invalid usable blocks";
    default:
      return "unknown error";
  }
}

zx_status_t ValidateHeader(const gpt_header_t* header, uint64_t block_count) {
  if (header->magic != kMagicNumber) {
    return ZX_ERR_BAD_STATE;
  }

  if (header->size != sizeof(gpt_header_t)) {
    return ZX_ERR_INVALID_ARGS;
  }

  gpt_header_t copy;
  memcpy(&copy, header, sizeof(gpt_header_t));
  copy.crc32 = 0;
  uint32_t crc = crc32(0, reinterpret_cast<uint8_t*>(&copy), copy.size);
  if (crc != header->crc32) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (header->entries_count > kPartitionCount) {
    return ZX_ERR_IO_OVERRUN;
  }

  if (header->entries_size != kEntrySize) {
    return ZX_ERR_FILE_BIG;
  }

  if (header->current >= block_count || header->backup >= block_count) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  if (header->first > header->last) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  return ZX_OK;
}

fit::result<uint64_t, zx_status_t> EntryBlockCount(const gpt_entry_t* entry) {
  if (entry == nullptr) {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }
  auto result = ValidateEntry(entry);
  if (result.is_error()) {
    return fit::error(ZX_ERR_BAD_STATE);
  }

  if (result.value() == false) {
    return fit::error(ZX_ERR_NOT_FOUND);
  }

  return fit::ok(entry->last - entry->first + 1);
}

fit::result<bool, zx_status_t> ValidateEntry(const gpt_entry_t* entry) {
  guid_t zero_guid = {};
  bool guid_valid = memcmp(entry->guid, &zero_guid, sizeof(zero_guid)) != 0;
  bool type_valid = memcmp(entry->type, &zero_guid, sizeof(zero_guid)) != 0;
  bool range_valid = (entry->first != 0) && (entry->first <= entry->last);

  if (!guid_valid && !type_valid && !range_valid) {
    // None of the fields are initialized. It is unused entry but this is not
    // an error case.
    return fit::ok(false);
  }

  // Guid is one/few of the fields that is uninitialized.
  if (!guid_valid) {
    return fit::error(ZX_ERR_BAD_STATE);
  }

  // Type is one/few of the fields that is uninitialized.
  if (!type_valid) {
    return fit::error(ZX_ERR_BAD_STATE);
  }

  // The range seems to be the problem.
  if (!range_valid) {
    return fit::error(ZX_ERR_OUT_OF_RANGE);
  }

  // All fields are initialized and contain valid data.
  return fit::ok(true);
}

bool IsPartitionVisible(const gpt_partition_t* partition) {
  return !((partition->flags & kFlagHidden) == kFlagHidden);
}

void SetPartitionVisibility(gpt_partition_t* partition, bool visible) {
  if (visible) {
    partition->flags &= ~kFlagHidden;
  } else {
    partition->flags |= kFlagHidden;
  }
}

mbr::Mbr MakeProtectiveMbr(uint64_t blocks_in_disk) {
  mbr::Mbr mbr = {};
  mbr.partitions[0].chs_address_start[1] = 0x1;
  mbr.partitions[0].type = mbr::kPartitionTypeGptProtective;
  mbr.partitions[0].chs_address_end[0] = 0xff;
  mbr.partitions[0].chs_address_end[1] = 0xff;
  mbr.partitions[0].chs_address_end[2] = 0xff;

  // Protective MBR should start at sector 1, and extend to the end of the disk.
  // If the number of blocks exceeds 32-bits, we simply make it as large as possible.
  mbr.partitions[0].start_sector_lba = 1;
  mbr.partitions[0].num_sectors =
      static_cast<uint32_t>(std::min(0xffff'ffffUL, blocks_in_disk - 1));

  return mbr;
}

zx_status_t GptDevice::FinalizeAndSync(bool persist) {
  auto result = InitializePrimaryHeader(blocksize_, blocks_);

  if (result.is_error()) {
    return result.error();
  }
  // fill in the new header fields
  gpt_header_t header = result.value();

  if (valid_) {
    header.current = header_.current;
    header.backup = header_.backup;
    memcpy(header.guid, header_.guid, 16);
    header.entries = header_.entries;
  }

  // always write 128 entries in partition table
  size_t ptable_size = kPartitionCount * sizeof(gpt_partition_t);
  std::unique_ptr<gpt_partition_t[]> buf(new gpt_partition_t[kPartitionCount]);
  if (!buf) {
    return ZX_ERR_NO_MEMORY;
  }
  memset(buf.get(), 0, ptable_size);

  // generate partition table
  uint8_t* ptr = reinterpret_cast<uint8_t*>(buf.get());
  for (uint32_t i = 0; i < kPartitionCount && partitions_[i] != NULL; i++) {
    memcpy(ptr, partitions_[i], kEntrySize);
    ptr += kEntrySize;
  }

  header.entries_crc = crc32(0, reinterpret_cast<uint8_t*>(buf.get()), ptable_size);

  uint64_t ptable_blocks = ptable_size / blocksize_;
  header.first = header.entries + ptable_blocks;
  header.last = header.backup - ptable_blocks - 1;

  // calculate header checksum
  header.crc32 = 0;
  header.crc32 = crc32(0, reinterpret_cast<const uint8_t*>(&header), kHeaderSize);

  // the copy cached in priv is the primary copy
  memcpy(&header_, &header, sizeof(header));

  // the header copy on stack is now the backup copy...
  header.current = header_.backup;
  header.backup = header_.current;
  header.entries = header_.last + 1;
  header.crc32 = 0;
  header.crc32 = crc32(0, reinterpret_cast<const uint8_t*>(&header), kHeaderSize);

  if (persist) {
    // Write protective MBR.
    mbr::Mbr mbr = MakeProtectiveMbr(blocks_);
    zx_status_t status =
        write_partial_block(fd_.get(), &mbr, sizeof(mbr), /*offset=*/0, blocksize_);
    if (status != ZX_OK) {
      return status;
    }

    // write backup to disk
    status = gpt_sync_current(fd_.get(), blocksize_, &header, buf.get());
    if (status != ZX_OK) {
      return status;
    }

    // write primary copy to disk
    status = gpt_sync_current(fd_.get(), blocksize_, &header_, buf.get());
    if (status != ZX_OK) {
      return status;
    }
  }

  // align backup with new on-disk state
  memcpy(ptable_backup_, ptable_, sizeof(ptable_));

  valid_ = true;

  return ZX_OK;
}

void GptDevice::PrintTable() const {
  int count = 0;
  for (; partitions_[count] != NULL; ++count)
    ;
  print_array(partitions_, count);
}

bool RangesOverlapsWithOtherRanges(const fbl::Vector<BlockRange>& ranges, const BlockRange& range) {
  for (auto r = ranges.begin(); r != ranges.end(); r++) {
    if (Overlap(*r, range)) {
      return true;
    }
  }

  return false;
}

zx_status_t GptDevice::ValidateEntries(const uint8_t* buffer) const {
  ZX_DEBUG_ASSERT(buffer != nullptr);
  ZX_DEBUG_ASSERT(!valid_);

  const gpt_partition_t* partitions = reinterpret_cast<const gpt_partition_t*>(buffer);

  fbl::Vector<BlockRange> ranges;
  // Range is half-open and gpt is close range. Add 1 to last.
  BlockRange usable_range(header_.first, header_.last + 1);

  // We should be here after we have validated the header. Length should be
  // greater than zero.
  ZX_ASSERT(usable_range.Length() > 0);

  // Verify crc before we process entries.
  if (header_.entries_crc != crc32(0, buffer, EntryArraySize())) {
    return ZX_ERR_BAD_STATE;
  }

  // Entries are not guaranteed to be sorted. We have to validate the range
  // of blocks they occupy by comparing each valid partition against all others.
  for (uint32_t i = 0; i < header_.entries_count; i++) {
    const gpt_entry_t* entry = &partitions[i];

    auto result = ValidateEntry(entry);
    // It is ok to have an empty entry but it is not ok entry.
    if (result.is_error()) {
      return result.error();
    }
    if (result.value() == false) {
      continue;
    }

    // Range is half-open and gpt is close range. Add 1 to last.
    BlockRange range(entry->first, entry->last + 1);

    // Entry's first block should be greater than or equal to GPT's first usable block.
    // Entry's last block should be less than or equal to GPT's last usable block.
    if (!Contains(usable_range, range)) {
      return ZX_ERR_ALREADY_EXISTS;
    }

    if (RangesOverlapsWithOtherRanges(ranges, range)) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    ranges.push_back(range);
  }

  return ZX_OK;
}

zx_status_t GptDevice::LoadEntries(const uint8_t* buffer, uint64_t buffer_size) {
  zx_status_t status;
  size_t entries_size = header_.entries_count * kEntrySize;

  // Ensure that we have large buffer that can contain all the
  // entries in the GPT.
  if (buffer_size < entries_size) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  if ((status = ValidateEntries(buffer)) != ZX_OK) {
    return status;
  }

  memcpy(ptable_, buffer, entries_size);

  // save original state so we can know what we changed
  memcpy(ptable_backup_, buffer, entries_size);

  // fill the table of valid partitions
  for (unsigned i = 0; i < header_.entries_count; i++) {
    auto result = ValidateEntry(&ptable_[i]);
    // It is ok to have an empty entry but not invalid entry.
    if (result.is_error()) {
      return result.error();
    } else if (result.value() == false) {
      continue;
    }
    partitions_[i] = &ptable_[i];
  }

  return ZX_OK;
}

zx_status_t GptDevice::GetDiffs(uint32_t idx, uint32_t* diffs) const {
  *diffs = 0;

  if (idx >= kPartitionCount) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (partitions_[idx] == NULL) {
    return ZX_ERR_NOT_FOUND;
  }

  const gpt_partition_t* a = partitions_[idx];
  const gpt_partition_t* b = &ptable_backup_[idx];
  if (memcmp(a->type, b->type, sizeof(a->type))) {
    *diffs |= kGptDiffType;
  }
  if (memcmp(a->guid, b->guid, sizeof(a->guid))) {
    *diffs |= kGptDiffGuid;
  }
  if (a->first != b->first) {
    *diffs |= kGptDiffFirst;
  }
  if (a->last != b->last) {
    *diffs |= kGptDiffLast;
  }
  if (a->flags != b->flags) {
    *diffs |= kGptDiffFlags;
  }
  if (memcmp(a->name, b->name, sizeof(a->name))) {
    *diffs |= kGptDiffName;
  }

  return ZX_OK;
}

zx_status_t GptDevice::Init(int fd, uint32_t blocksize, uint64_t block_count,
                            std::unique_ptr<GptDevice>* out_dev) {
  ssize_t ret;
  off_t offset;

  fbl::unique_fd fdp(dup(fd));
  if (!fdp.is_valid()) {
    G_PRINTF("failed to dup the fd\n");
    return ZX_ERR_INTERNAL;
  }

  uint8_t block[blocksize];

  if (blocksize < kMinimumBlockSize) {
    G_PRINTF("blocksize < %u not supported\n", kMinimumBlockSize);
    return ZX_ERR_INTERNAL;
  }

  if (blocksize > kMaximumBlockSize) {
    G_PRINTF("blocksize > %u not supported\n", kMaximumBlockSize);
    return ZX_ERR_INTERNAL;
  }

  offset = 0;
  ret = pread(fdp.get(), block, blocksize, offset);
  if (ret != blocksize) {
    return ZX_ERR_IO;
  }

  // read the gpt header (lba 1)
  offset = kPrimaryHeaderStartBlock * blocksize;
  ssize_t size = MinimumBytesPerCopy(blocksize).value();
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[size]);
  ret = pread(fdp.get(), buffer.get(), size, offset);

  if (ret != size) {
    return ZX_ERR_IO;
  }

  std::unique_ptr<GptDevice> dev;
  zx_status_t status = Load(buffer.get(), size, blocksize, block_count, &dev);

  if (status != ZX_OK) {
    // We did not find valid gpt on the file. Initialize new gpt.
    G_PRINTF("%s\n", HeaderStatusToCString(status));
    dev.reset(new GptDevice());
    dev->blocksize_ = blocksize;
    dev->blocks_ = block_count;
  }
  dev->fd_ = std::move(fdp);
  *out_dev = std::move(dev);
  return ZX_OK;
}

zx_status_t GptDevice::Create(int fd, uint32_t blocksize, uint64_t blocks,
                              std::unique_ptr<GptDevice>* out) {
  return Init(fd, blocksize, blocks, out);
}

zx_status_t GptDevice::Load(const uint8_t* buffer, uint64_t buffer_size, uint32_t blocksize,
                            uint64_t blocks, std::unique_ptr<GptDevice>* out) {
  if (buffer == nullptr || out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (blocksize < kHeaderSize) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (buffer_size < kHeaderSize) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = ValidateHeader(reinterpret_cast<const gpt_header_t*>(buffer), blocks);
  if (status != ZX_OK) {
    return status;
  }

  std::unique_ptr<GptDevice> dev(new GptDevice());
  memcpy(&dev->header_, buffer, sizeof(gpt_header_t));

  if ((status = dev->LoadEntries(&buffer[blocksize], buffer_size - blocksize)) != ZX_OK) {
    return status;
  }

  dev->blocksize_ = blocksize;
  dev->blocks_ = blocks;

  dev->valid_ = true;
  *out = std::move(dev);

  return status;
}

zx_status_t GptDevice::Finalize() { return FinalizeAndSync(false); }

zx_status_t GptDevice::Sync() { return FinalizeAndSync(true); }

zx_status_t GptDevice::Range(uint64_t* block_start, uint64_t* block_end) const {
  if (!valid_) {
    G_PRINTF("partition header invalid\n");
    return ZX_ERR_INTERNAL;
  }

  // check range
  *block_start = header_.first;
  *block_end = header_.last;
  return ZX_OK;
}

zx_status_t GptDevice::AddPartition(const char* name, const uint8_t* type, const uint8_t* guid,
                                    uint64_t offset, uint64_t blocks, uint64_t flags) {
  if (!valid_) {
    G_PRINTF("partition header invalid, sync to generate a default header\n");
    return ZX_ERR_INTERNAL;
  }

  if (blocks == 0) {
    G_PRINTF("partition must be at least 1 block\n");
    return ZX_ERR_INVALID_ARGS;
  }

  uint64_t first = offset;
  uint64_t last = first + blocks - 1;

  // check range
  if (last < first || first < header_.first || last > header_.last) {
    G_PRINTF("partition must be in range of usable blocks[%" PRIu64 ", %" PRIu64 "]\n",
             header_.first, header_.last);
    return ZX_ERR_INVALID_ARGS;
  }

  // check for overlap
  uint32_t i;
  int tail = -1;
  for (i = 0; i < kPartitionCount; i++) {
    if (!partitions_[i]) {
      tail = i;
      break;
    }
    if (first <= partitions_[i]->last && last >= partitions_[i]->first) {
      G_PRINTF("partition range overlaps\n");
      return ZX_ERR_OUT_OF_RANGE;
    }
  }
  if (tail == -1) {
    G_PRINTF("too many partitions\n");
    return ZX_ERR_OUT_OF_RANGE;
  }

  // find a free slot
  gpt_partition_t* part = NULL;
  for (i = 0; i < kPartitionCount; i++) {
    if (ptable_[i].first == 0 && ptable_[i].last == 0) {
      part = &ptable_[i];
      break;
    }
  }
  assert(part);

  // insert the new element into the list
  partition_init(part, name, type, guid, first, last, flags);
  partitions_[tail] = part;
  return ZX_OK;
}

zx_status_t GptDevice::ClearPartition(uint64_t offset, uint64_t blocks) {
  if (!valid_) {
    G_PRINTF("partition header invalid, sync to generate a default header\n");
    return ZX_ERR_WRONG_TYPE;
  }

  if (blocks == 0) {
    G_PRINTF("must clear at least 1 block\n");
    return ZX_ERR_NO_RESOURCES;
  }
  uint64_t first = offset;
  uint64_t last = offset + blocks - 1;

  if (last < first || first < header_.first || last > header_.last) {
    G_PRINTF("must clear in the range of usable blocks[%" PRIu64 ", %" PRIu64 "]\n", header_.first,
             header_.last);
    return ZX_ERR_OUT_OF_RANGE;
  }

  char zero[blocksize_];
  memset(zero, 0, sizeof(zero));

  for (size_t i = first; i <= last; i++) {
    if (pwrite(fd_.get(), zero, sizeof(zero), blocksize_ * i) !=
        static_cast<ssize_t>(sizeof(zero))) {
      G_PRINTF("Failed to write to block %zu; errno: %d\n", i, errno);
      return ZX_ERR_IO;
    }
  }

  return ZX_OK;
}

zx_status_t GptDevice::RemovePartition(const uint8_t* guid) {
  // look for the entry in the partition list
  uint32_t i;
  for (i = 0; i < kPartitionCount; i++) {
    if (!memcmp(partitions_[i]->guid, guid, sizeof(partitions_[i]->guid))) {
      break;
    }
  }
  if (i == kPartitionCount) {
    G_PRINTF("partition not found\n");
    return ZX_ERR_NOT_FOUND;
  }
  // clear the entry
  memset(partitions_[i], 0, kEntrySize);
  // pack the partition list
  for (i = i + 1; i < kPartitionCount; i++) {
    if (partitions_[i] == NULL) {
      partitions_[i - 1] = NULL;
    } else {
      partitions_[i - 1] = partitions_[i];
    }
  }
  return ZX_OK;
}

zx_status_t GptDevice::RemoveAllPartitions() {
  memset(partitions_, 0, sizeof(partitions_));
  return ZX_OK;
}

gpt_partition_t* GptDevice::GetPartition(uint32_t partition_index) const {
  if (partition_index >= kPartitionCount) {
    return nullptr;
  }

  return partitions_[partition_index];
}

zx_status_t GptDevice::SetPartitionType(uint32_t partition_index, const uint8_t* type) {
  gpt_partition_t* p = GetPartition(partition_index);
  if (p == nullptr) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  memcpy(p->type, type, GPT_GUID_LEN);
  return ZX_OK;
}

zx_status_t GptDevice::SetPartitionGuid(uint32_t partition_index, const uint8_t* guid) {
  gpt_partition_t* p = GetPartition(partition_index);
  if (p == nullptr) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  memcpy(p->guid, guid, GPT_GUID_LEN);
  return ZX_OK;
}

zx_status_t GptDevice::SetPartitionVisibility(uint32_t partition_index, bool visible) {
  gpt_partition_t* p = GetPartition(partition_index);
  if (p == nullptr) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  gpt::SetPartitionVisibility(p, visible);

  return ZX_OK;
}

zx_status_t GptDevice::SetPartitionRange(uint32_t partition_index, uint64_t start, uint64_t end) {
  gpt_partition_t* p = GetPartition(partition_index);
  if (p == nullptr) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  zx_status_t ret;
  uint64_t block_start, block_end;
  if ((ret = Range(&block_start, &block_end)) != ZX_OK) {
    return ret;
  }

  if ((start < block_start) || (end > block_end) || (start >= end)) {
    return ZX_ERR_INVALID_ARGS;
  }

  for (uint32_t idx = 0; idx < kPartitionCount; idx++) {
    // skip this partition and non-existent partitions
    if ((idx == partition_index) || (GetPartition(idx) == NULL)) {
      continue;
    }

    // skip partitions we don't intersect
    if ((start > GetPartition(idx)->last) || (end < GetPartition(idx)->first)) {
      continue;
    }

    return ZX_ERR_OUT_OF_RANGE;
  }

  p->first = start;
  p->last = end;
  return ZX_OK;
}

zx_status_t GptDevice::GetPartitionFlags(uint32_t partition_index, uint64_t* flags) const {
  gpt_partition_t* p = GetPartition(partition_index);
  if (p == nullptr) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  *flags = p->flags;
  return ZX_OK;
}

// TODO(auradkar): flags are unckecked for invalid flags
zx_status_t GptDevice::SetPartitionFlags(uint32_t partition_index, uint64_t flags) {
  gpt_partition_t* p = GetPartition(partition_index);
  if (p == nullptr) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  p->flags = flags;
  return ZX_OK;
}

void GptDevice::GetHeaderGuid(uint8_t (*disk_guid_out)[GPT_GUID_LEN]) const {
  memcpy(disk_guid_out, header_.guid, GPT_GUID_LEN);
}

}  // namespace gpt
