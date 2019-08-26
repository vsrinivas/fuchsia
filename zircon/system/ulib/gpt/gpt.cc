// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpt/gpt.h"

#include <assert.h>
#include <errno.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <inttypes.h>
#include <lib/cksum.h>
#include <lib/fzl/fdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>  // for zx_cprng_draw

#include <gpt/gpt.h>
#include <gpt/guid.h>

namespace gpt {

namespace {
#define G_PRINTF(f, ...) \
  if (debug_out)         \
    printf((f), ##__VA_ARGS__);

bool debug_out = false;

struct mbr_partition_t {
  uint8_t status;
  uint8_t chs_first[3];
  uint8_t type;
  uint8_t chs_last[3];
  uint32_t lba;
  uint32_t sectors;
};

void print_array(const gpt_partition_t* const a[kPartitionCount], int c) {
  char GUID[GPT_GUID_STRLEN];
  char name[GPT_NAME_LEN / 2 + 1];

  for (int i = 0; i < c; ++i) {
    uint8_to_guid_string(GUID, a[i]->type);
    memset(name, 0, GPT_NAME_LEN / 2 + 1);
    utf16_to_cstring(name, reinterpret_cast<const uint16_t*>(a[i]->name), GPT_NAME_LEN / 2);

    printf("Name: %s \n  Start: %lu -- End: %lu \nType: %s\n", name, a[i]->first, a[i]->last, GUID);
  }
}

void partition_init(gpt_partition_t* part, const char* name, const uint8_t* type,
                    const uint8_t* guid, uint64_t first, uint64_t last, uint64_t flags) {
  memcpy(part->type, type, sizeof(part->type));
  memcpy(part->guid, guid, sizeof(part->guid));
  part->first = first;
  part->last = last;
  part->flags = flags;
  cstring_to_utf16(reinterpret_cast<uint16_t*>(part->name), name,
                   sizeof(part->name) / sizeof(uint16_t));
}

zx_status_t gpt_sync_current(int fd, uint64_t blocksize, gpt_header_t* header,
                             gpt_partition_t* ptable) {
  // write partition table first
  ssize_t ret;
  off_t offset;
  offset = header->entries * blocksize;
  size_t ptable_size = header->entries_count * header->entries_size;
  ret = pwrite(fd, ptable, ptable_size, offset);
  if (ret != static_cast<ssize_t>(ptable_size)) {
    return ZX_ERR_IO;
  }

  // then write the header
  offset = header->current * blocksize;

  uint8_t block[blocksize];
  memset(block, 0, sizeof(blocksize));
  memcpy(block, header, sizeof(*header));
  ret = pwrite(fd, block, blocksize, offset);
  if (ret != static_cast<ssize_t>(blocksize)) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
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

void uint8_to_guid_string(char* dst, const uint8_t* src) {
  const guid_t* guid = reinterpret_cast<const guid_t*>(src);
  sprintf(dst, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid->data1, guid->data2,
          guid->data3, guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3],
          guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
}

const char* gpt_guid_to_type(const char* guid) { return gpt::KnownGuid::GuidStrToName(guid); }

__END_CDECLS

uint64_t MinimumRequiredBlocksPerCopy(uint64_t block_size) {
  uint64_t header_blocks = kHeaderBlocks;
  uint64_t table_blocks = ((kMaxPartitionTableSize + block_size - 1) / block_size);
  return header_blocks + table_blocks;
}

uint64_t MinimumRequiredBlocks(uint64_t block_size) {
  // There are two copies of GPT and a block for MBR(or such use).
  return kPrimaryHeaderStartBlock + (2 * MinimumRequiredBlocksPerCopy(block_size));
}

fit::result<gpt_header_t, zx_status_t> InitializePrimaryHeader(uint64_t block_size,
                                                               uint64_t block_count) {
  gpt_header_t header = {};

  if (block_size < kHeaderSize) {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  if (block_count <= MinimumRequiredBlocks(block_size)) {
    return fit::error(ZX_ERR_BUFFER_TOO_SMALL);
  }

  header.magic = kMagicNumber;
  header.revision = kRevision;
  header.size = kHeaderSize;
  header.current = kPrimaryHeaderStartBlock;

  // backup gpt is in the last block
  header.backup = block_count - 1;

  // First usable block is the block after end of primary copy.
  header.first = kPrimaryHeaderStartBlock + MinimumRequiredBlocksPerCopy(block_size);

  // Last usable block is the block before beginning of backup entries array.
  header.last = block_count - MinimumRequiredBlocksPerCopy(block_size) - 1;

  // We have ensured above that there are more blocks than MinimumRequiredBlocks().
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

  return ZX_OK;
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

zx_status_t GptDevice::FinalizeAndSync(bool persist) {
  // write fake mbr if needed
  uint8_t mbr[blocksize_];
  off_t offset;
  ssize_t ret;
  if (!mbr_) {
    memset(mbr, 0, blocksize_);
    mbr[0x1fe] = 0x55;
    mbr[0x1ff] = 0xaa;
    mbr_partition_t* mpart = reinterpret_cast<mbr_partition_t*>(mbr + 0x1be);
    mpart->chs_first[1] = 0x1;
    mpart->type = 0xee;  // gpt protective mbr
    mpart->chs_last[0] = 0xfe;
    mpart->chs_last[1] = 0xff;
    mpart->chs_last[2] = 0xff;
    mpart->lba = 1;
    mpart->sectors = blocks_ & 0xffffffff;
    offset = 0;
    ret = pwrite(fd_.get(), mbr, blocksize_, offset);
    if (ret != static_cast<ssize_t>(blocksize_)) {
      return ZX_ERR_IO;
    }
    mbr_ = true;
  }

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
  fbl::unique_ptr<gpt_partition_t[]> buf(new gpt_partition_t[kPartitionCount]);
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
    zx_status_t status;
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

zx_status_t GptDevice::Init(int fd, uint32_t blocksize, uint64_t blocks) {
  ssize_t ptable_size;
  uint32_t crc;
  gpt_partition_t* ptable;
  gpt_header_t* header;
  ssize_t ret;
  off_t offset;

  fd_.reset(dup(fd));
  if (!fd_.is_valid()) {
    G_PRINTF("failed to dup the fd\n");
    return ZX_ERR_INTERNAL;
  }

  blocksize_ = blocksize;
  blocks_ = blocks;

  uint8_t block[blocksize];

  if (blocksize_ < 512) {
    G_PRINTF("blocksize < 512 not supported\n");
    return ZX_ERR_INTERNAL;
  }

  offset = 0;
  ret = pread(fd_.get(), block, blocksize, offset);
  if (ret != blocksize) {
    return ZX_ERR_IO;
  }
  mbr_ = block[0x1fe] == 0x55 && block[0x1ff] == 0xaa;

  // read the gpt header (lba 1)
  offset = blocksize;
  ret = pread(fd_.get(), block, blocksize, offset);
  if (ret != blocksize) {
    return ZX_ERR_IO;
  }

  header = &header_;
  memcpy(header, block, sizeof(*header));
  zx_status_t status = ValidateHeader(header, blocks);

  // Invalid header.
  if (status != ZX_OK) {
    G_PRINTF("%s\n", HeaderStatusToCString(status));
    return ZX_OK;
  }

  valid_ = true;

  if (header->entries_count == 0) {
    return ZX_OK;
  }

  ptable = ptable_;
  ptable_size = header->entries_size * header->entries_count;
  if (static_cast<size_t>(ptable_size) > SIZE_MAX) {
    G_PRINTF("partition table too big\n");
    return ZX_OK;
  }

  // read the partition table
  offset = header->entries * blocksize;
  ret = pread(fd_.get(), ptable, ptable_size, offset);
  if (ret != ptable_size) {
    return ZX_ERR_IO;
  }

  // partition table checksum
  crc = crc32(0, reinterpret_cast<const uint8_t*>(ptable), ptable_size);
  if (crc != header->entries_crc) {
    G_PRINTF("table crc check failed\n");
    return ZX_OK;
  }

  // save original state so we can know what we changed
  memcpy(ptable_backup_, ptable_, sizeof(ptable_));

  // fill the table of valid partitions
  for (unsigned i = 0; i < header->entries_count; i++) {
    if (ptable[i].first == 0 && ptable[i].last == 0)
      continue;
    partitions_[i] = &ptable[i];
  }
  return ZX_OK;
}

zx_status_t GptDevice::Create(int fd, uint32_t blocksize, uint64_t blocks,
                              fbl::unique_ptr<GptDevice>* out) {
  fbl::unique_ptr<GptDevice> dev(new GptDevice());
  zx_status_t status = dev->Init(fd, blocksize, blocks);
  if (status == ZX_OK) {
    *out = std::move(dev);
  }

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
