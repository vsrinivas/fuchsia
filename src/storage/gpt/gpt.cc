// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpt/gpt.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <lib/cksum.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>  // for zx_cprng_draw

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>

#include <fbl/algorithm.h>
#include <fbl/vector.h>
#include <gpt/gpt.h>
#include <gpt/guid.h>
#include <mbr/mbr.h>
#include <range/range.h>
#include <safemath/checked_math.h>
#include <src/lib/uuid/uuid.h>

#include "gpt.h"
#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/lib/utf_conversion/utf_conversion.h"

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
zx_status_t write_partial_block(int fd, void* data, size_t size, off_t offset, size_t blocksize) {
  // If input block is already rounded to blocksize, just directly write from our buffer.
  if (size % blocksize == 0) {
    if (block_client::SingleWriteBytes(fd, data, size, offset) != ZX_OK) {
      return ZX_ERR_IO;
    }
    return ZX_OK;
  }

  // Otherwise, pad out data to blocksize.
  size_t new_size = fbl::round_up(size, blocksize);
  std::unique_ptr<uint8_t[]> block(new uint8_t[new_size]);
  memcpy(block.get(), data, size);
  memset(block.get() + size, 0, new_size - size);
  if (block_client::SingleWriteBytes(fd, block.get(), new_size, offset) != ZX_OK) {
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
  // Check all offset calculations are valid
  off_t table_offset;
  if (!safemath::CheckMul(header->entries, blocksize).Cast<off_t>().AssignIfValid(&table_offset)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  size_t ptable_size;
  if (!safemath::CheckMul(header->entries_count, header->entries_size)
           .Cast<size_t>()
           .AssignIfValid(&ptable_size)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  off_t header_offset;
  if (!safemath::CheckMul(header->current, blocksize).Cast<off_t>().AssignIfValid(&header_offset)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Write partition table first
  zx_status_t status = write_partial_block(fd, ptable, ptable_size, table_offset, blocksize);
  if (status != ZX_OK) {
    return status;
  }

  // Then write the header
  return write_partial_block(fd, header, sizeof(*header), header_offset, blocksize);
}

int compare(const void* ls, const void* rs) {
  const auto* l = *static_cast<gpt_partition_t* const*>(ls);
  const auto* r = *static_cast<gpt_partition_t* const*>(rs);
  if (l == nullptr && r == nullptr) {
    return 0;
  }

  if (l == nullptr) {
    return 1;
  }

  if (r == nullptr) {
    return -1;
  }

  if (l->first == r->first) {
    return 0;
  }

  return (l->first < r->first) ? -1 : 1;
}

// Returns a copy of |str| with any hex values converted to uppercase.
std::string HexToUpper(std::string str) {
  for (char& ch : str) {
    if (ch >= 'a' && ch <= 'f') {
      ch -= 'a';
      ch += 'A';
    }
  }
  return str;
}

// Converts a GPT inclusive range [start, end] to an end-exclusive range::Range [start, end).
// Returns std::nullopt if the range conflicts with GPT headers or exceeds the device's block size.
std::optional<BlockRange> ConvertBlockRange(uint64_t start_block, uint64_t end_block,
                                            uint64_t block_count) {
  if (block_count == 0)
    return std::nullopt;

  if (start_block > end_block)
    return std::nullopt;

  if (start_block < kPrimaryEntriesStartBlock)
    return std::nullopt;

  const uint64_t backup_header_block = block_count - 1;

  // Backup GPT header should be in the last block in the device.
  if (start_block >= backup_header_block || end_block >= backup_header_block)
    return std::nullopt;

  // Overflow shouldn't be possible due to validating against backup_header_block.
  return BlockRange{start_block, safemath::CheckAdd(end_block, 1).ValueOrDie()};
}

}  // namespace

__BEGIN_CDECLS

void gpt_set_debug_output_enabled(bool enabled) { debug_out = enabled; }

void gpt_sort_partitions(const gpt_partition_t** partitions, size_t count) {
  qsort(partitions, count, sizeof(gpt_partition_t*), compare);
}

// TODO(69527): migrate usages to |utf8_to_utf16| in utf_conversion.h
void cstring_to_utf16(uint16_t* dst, const char* src, size_t maxlen) {
  size_t len = strlen(src);
  if (len > maxlen) {
    len = maxlen - 1;
  }
  for (size_t i = 0; i < len; i++) {
    dst[i] = static_cast<uint16_t>(src[i] & 0x7f);
  }
  dst[len] = 0;
}

// TODO(69527): migrate usages to |utf16_to_utf8| in utf_conversion.h
char* utf16_to_cstring(char* dst, const uint16_t* src, size_t len) {
  size_t i = 0;
  char* ptr = dst;
  while (i < len) {
    char c = static_cast<char>(src[i++] & 0x7f);
    *ptr++ = c;
    if (!c) {
      break;
    }
  }
  return dst;
}

bool gpt_is_sys_guid(const uint8_t* guid, ssize_t len) {
  static const uint8_t sys_guid[GPT_GUID_LEN] = GUID_SYSTEM_VALUE;
  return len == GPT_GUID_LEN && !memcmp(guid, sys_guid, GPT_GUID_LEN);
}

bool gpt_is_data_guid(const uint8_t* guid, ssize_t len) {
  static const uint8_t data_guid[GPT_GUID_LEN] = GUID_DATA_VALUE;
  return len == GPT_GUID_LEN && !memcmp(guid, data_guid, GPT_GUID_LEN);
}

bool gpt_is_durable_guid(const uint8_t* guid, ssize_t len) {
  static const uint8_t durable_guid[GPT_GUID_LEN] = GPT_DURABLE_TYPE_GUID;
  return len == GPT_GUID_LEN && !memcmp(guid, durable_guid, GPT_GUID_LEN);
}

bool gpt_is_efi_guid(const uint8_t* guid, ssize_t len) {
  static const uint8_t efi_guid[GPT_GUID_LEN] = GUID_EFI_VALUE;
  return len == GPT_GUID_LEN && !memcmp(guid, efi_guid, GPT_GUID_LEN);
}

bool gpt_is_factory_guid(const uint8_t* guid, ssize_t len) {
  static const uint8_t factory_guid[GPT_GUID_LEN] = GPT_FACTORY_TYPE_GUID;
  return len == GPT_GUID_LEN && !memcmp(guid, factory_guid, GPT_GUID_LEN);
}

void uint8_to_guid_string(char* dst, const uint8_t* src) {
  strcpy(dst, HexToUpper(uuid::Uuid(src).ToString()).c_str());
}

__END_CDECLS

zx::result<> GetPartitionName(const gpt_entry_t& entry, char* name, size_t capacity) {
  size_t len = capacity;
  const uint16_t* utf16_name = reinterpret_cast<const uint16_t*>(entry.name);
  const uint16_t* utf16_name_end = utf16_name + (sizeof(entry.name) / sizeof(uint16_t));
  const size_t utf16_name_len = std::distance(utf16_name, std::find(utf16_name, utf16_name_end, 0));
  if (zx_status_t status =
          utf16_to_utf8(utf16_name, utf16_name_len, reinterpret_cast<uint8_t*>(name), &len,
                        UTF_CONVERT_FLAG_FORCE_LITTLE_ENDIAN);
      status != ZX_OK) {
    return zx::error(status);
  }
  if (len >= capacity) {
    return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
  }
  name[len] = 0;
  return zx::ok();
}

fpromise::result<gpt_header_t, zx_status_t> InitializePrimaryHeader(uint64_t block_size,
                                                                    uint64_t block_count) {
  gpt_header_t header = {};

  if (block_size < kHeaderSize) {
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }

  if (block_count <= MinimumBlockDeviceSize(block_size).value()) {
    return fpromise::error(ZX_ERR_BUFFER_TOO_SMALL);
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

  return fpromise::ok(header);
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
      return "invalid header (CRC or invalid range)";
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
  ZX_ASSERT(header != nullptr);

  if (header->magic != kMagicNumber)
    return ZX_ERR_BAD_STATE;

  if (header->size != sizeof(gpt_header_t) || block_count < kPrimaryEntriesStartBlock)
    return ZX_ERR_INVALID_ARGS;

  gpt_header_t copy;
  memcpy(&copy, header, sizeof(gpt_header_t));
  copy.crc32 = 0;
  uint32_t crc = crc32(0, reinterpret_cast<uint8_t*>(&copy), copy.size);
  if (crc != header->crc32)
    return ZX_ERR_IO_DATA_INTEGRITY;

  if (header->entries_count > kPartitionCount)
    return ZX_ERR_IO_OVERRUN;

  if (header->entries_size != kEntrySize)
    return ZX_ERR_FILE_BIG;

  if (header->current >= block_count || header->backup >= block_count)
    return ZX_ERR_BUFFER_TOO_SMALL;

  if (!ConvertBlockRange(header->first, header->last, block_count))
    return ZX_ERR_IO_DATA_INTEGRITY;

  return ZX_OK;
}

fpromise::result<uint64_t, zx_status_t> EntryBlockCount(const gpt_entry_t* entry) {
  if (entry == nullptr) {
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }
  auto result = ValidateEntry(entry);
  if (result.is_error()) {
    return fpromise::error(ZX_ERR_BAD_STATE);
  }

  if (!result.value()) {
    return fpromise::error(ZX_ERR_NOT_FOUND);
  }

  return fpromise::ok(entry->last - entry->first + 1);
}

fpromise::result<bool, zx_status_t> ValidateEntry(const gpt_entry_t* entry) {
  uuid::Uuid zero_guid;
  bool guid_valid = (uuid::Uuid(entry->guid) != zero_guid);
  bool type_valid = (uuid::Uuid(entry->type) != zero_guid);
  bool range_valid = (entry->first != 0) && (entry->first <= entry->last);

  if (!guid_valid && !type_valid && !range_valid) {
    // None of the fields are initialized. It is unused entry but this is not
    // an error case.
    return fpromise::ok(false);
  }

  // Guid is one/few of the fields that is uninitialized.
  if (!guid_valid) {
    return fpromise::error(ZX_ERR_BAD_STATE);
  }

  // Type is one/few of the fields that is uninitialized.
  if (!type_valid) {
    return fpromise::error(ZX_ERR_BAD_STATE);
  }

  // The range seems to be the problem.
  if (!range_valid) {
    return fpromise::error(ZX_ERR_OUT_OF_RANGE);
  }

  // All fields are initialized and contain valid data.
  return fpromise::ok(true);
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
  for (uint32_t i = 0; i < kPartitionCount && partitions_[i] != nullptr; i++) {
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
  for (; partitions_[count] != nullptr; ++count)
    ;
  print_array(partitions_, count);
}

bool RangesOverlapsWithOtherRanges(const fbl::Vector<BlockRange>& ranges, const BlockRange& range) {
  return std::any_of(ranges.begin(), ranges.end(),
                     [&range](const BlockRange& r) { return Overlap(r, range); });
}

zx_status_t GptDevice::ValidateEntries(const uint8_t* buffer, uint64_t block_count) const {
  ZX_DEBUG_ASSERT(buffer != nullptr);
  ZX_DEBUG_ASSERT(!valid_);

  const gpt_partition_t* partitions = reinterpret_cast<const gpt_partition_t*>(buffer);

  fbl::Vector<BlockRange> ranges;
  std::optional<BlockRange> usable_range =
      ConvertBlockRange(header_.first, header_.last, block_count);

  // We should be here after we have validated the header.
  ZX_ASSERT(usable_range);

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
    if (!result.value()) {
      continue;
    }

    // Ensure partition range doesn't conflict with device size or GPT headers.
    std::optional<BlockRange> partition_range =
        ConvertBlockRange(entry->first, entry->last, block_count);
    if (!partition_range)
      return ZX_ERR_IO_DATA_INTEGRITY;

    // Entry's first block should be greater than or equal to GPT's first usable block.
    // Entry's last block should be less than or equal to GPT's last usable block.
    if (!Contains(*usable_range, *partition_range)) {
      return ZX_ERR_ALREADY_EXISTS;
    }

    if (RangesOverlapsWithOtherRanges(ranges, *partition_range)) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    ranges.push_back(*partition_range);
  }

  return ZX_OK;
}

zx_status_t GptDevice::LoadEntries(const uint8_t* buffer, uint64_t buffer_size,
                                   uint64_t block_count) {
  zx_status_t status;
  size_t entries_size = static_cast<size_t>(header_.entries_count) * kEntrySize;

  // Ensure that we have large buffer that can contain all the
  // entries in the GPT.
  if (buffer_size < entries_size) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  if ((status = ValidateEntries(buffer, block_count)) != ZX_OK) {
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
    }
    if (!result.value()) {
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

  if (partitions_[idx] == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }

  const gpt_partition_t* a = partitions_[idx];
  const gpt_partition_t* b = &ptable_backup_[idx];
  if (memcmp(a->type, b->type, sizeof(a->type)) != 0) {
    *diffs |= kGptDiffType;
  }
  if (memcmp(a->guid, b->guid, sizeof(a->guid)) != 0) {
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
  if (memcmp(a->name, b->name, sizeof(a->name)) != 0) {
    *diffs |= kGptDiffName;
  }

  return ZX_OK;
}

zx_status_t GptDevice::Init(int fd, uint32_t blocksize, uint64_t block_count,
                            std::unique_ptr<GptDevice>* out_dev) {
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
  if (block_client::SingleReadBytes(fdp.get(), block, blocksize, offset) != ZX_OK) {
    return ZX_ERR_IO;
  }

  // read the gpt header (lba 1)
  offset = static_cast<off_t>(kPrimaryHeaderStartBlock) * blocksize;
  size_t size = MinimumBytesPerCopy(blocksize).value();
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[size]);
  if (block_client::SingleReadBytes(fdp.get(), buffer.get(), size, offset) != ZX_OK) {
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

  if ((status = dev->LoadEntries(&buffer[blocksize], buffer_size - blocksize, blocks)) != ZX_OK) {
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
  std::optional<uint32_t> tail;
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
  if (!tail) {
    G_PRINTF("too many partitions\n");
    return ZX_ERR_OUT_OF_RANGE;
  }

  // find a free slot
  gpt_partition_t* part = nullptr;
  for (i = 0; i < kPartitionCount; i++) {
    if (ptable_[i].first == 0 && ptable_[i].last == 0) {
      part = &ptable_[i];
      break;
    }
  }
  assert(part);

  // insert the new element into the list
  partition_init(part, name, type, guid, first, last, flags);
  partitions_[tail.value()] = part;
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
    off_t offset;
    if (!safemath::CheckMul(blocksize_, i).Cast<off_t>().AssignIfValid(&offset)) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    auto status = block_client::SingleWriteBytes(fd_.get(), zero, sizeof(zero), offset);
    if (status != ZX_OK) {
      G_PRINTF("Failed to write to block %zu; errno: %d\n", i, status);
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
    if (partitions_[i] == nullptr) {
      partitions_[i - 1] = nullptr;
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

zx::result<gpt_partition_t*> GptDevice::GetPartitionPtr(uint32_t partition_index) const {
  if (partition_index >= kPartitionCount)
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  if (partitions_[partition_index] == nullptr)
    return zx::error(ZX_ERR_NOT_FOUND);
  return zx::ok(partitions_[partition_index]);
}

zx::result<gpt_partition_t*> GptDevice::GetPartition(uint32_t partition_index) {
  return GetPartitionPtr(partition_index);
}

zx::result<const gpt_partition_t*> GptDevice::GetPartition(uint32_t partition_index) const {
  return GetPartitionPtr(partition_index);
}

zx_status_t GptDevice::SetPartitionType(uint32_t partition_index, const uint8_t* type) {
  zx::result<gpt_partition_t*> p = GetPartition(partition_index);
  if (p.is_ok()) {
    memcpy((*p)->type, type, GPT_GUID_LEN);
  }
  return p.status_value();
}

zx_status_t GptDevice::SetPartitionGuid(uint32_t partition_index, const uint8_t* guid) {
  zx::result<gpt_partition_t*> p = GetPartition(partition_index);
  if (p.is_ok()) {
    memcpy((*p)->guid, guid, GPT_GUID_LEN);
  }
  return p.status_value();
}

zx_status_t GptDevice::SetPartitionVisibility(uint32_t partition_index, bool visible) {
  zx::result<gpt_partition_t*> p = GetPartition(partition_index);
  if (p.is_ok()) {
    gpt::SetPartitionVisibility(*p, visible);
  }
  return p.status_value();
}

zx_status_t GptDevice::SetPartitionRange(uint32_t partition_index, uint64_t start, uint64_t end) {
  zx::result<gpt_partition_t*> p = GetPartition(partition_index);
  if (p.is_error()) {
    return p.error_value();
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
    zx::result<const gpt_partition_t*> curr_partition = GetPartition(idx);
    // skip this partition and non-existent partitions
    if ((idx == partition_index) || (curr_partition.is_error())) {
      continue;
    }

    // skip partitions we don't intersect
    if ((start > (*curr_partition)->last) || (end < (*curr_partition)->first)) {
      continue;
    }

    return ZX_ERR_OUT_OF_RANGE;
  }

  (*p)->first = start;
  (*p)->last = end;
  return ZX_OK;
}

zx_status_t GptDevice::GetPartitionFlags(uint32_t partition_index, uint64_t* flags) const {
  zx::result<const gpt_partition_t*> p = GetPartition(partition_index);
  if (p.is_ok()) {
    *flags = (*p)->flags;
  }
  return p.status_value();
}

// TODO(auradkar): flags are unckecked for invalid flags
zx_status_t GptDevice::SetPartitionFlags(uint32_t partition_index, uint64_t flags) {
  zx::result<gpt_partition_t*> p = GetPartition(partition_index);
  if (p.is_ok()) {
    (*p)->flags = flags;
  }
  return p.status_value();
}

void GptDevice::GetHeaderGuid(uint8_t (*disk_guid_out)[GPT_GUID_LEN]) const {
  memcpy(disk_guid_out, header_.guid, GPT_GUID_LEN);
}

}  // namespace gpt
