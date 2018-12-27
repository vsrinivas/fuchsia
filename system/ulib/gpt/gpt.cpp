// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gpt/gpt.h>
#include <lib/cksum.h>
#include <zircon/syscalls.h> // for zx_cprng_draw
#include <zircon/device/block.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "gpt/gpt.h"

namespace gpt {

namespace {
#define G_PRINTF(f, ...) \
    if (debug_out)       \
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

// Since the human-readable representation of a GUID is the following format,
// ordered little-endian, it is useful to group a GUID into these
// appropriately-sized groups.
struct guid_t {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

static_assert(sizeof(gpt_header_t) == GPT_HEADER_SIZE, "unexpected gpt header size");

void print_array(const gpt_partition_t* const a[kPartitionCount], int c) {
    char GUID[GPT_GUID_STRLEN];
    char name[GPT_NAME_LEN / 2 + 1];

    for (int i = 0; i < c; ++i) {
        uint8_to_guid_string(GUID, a[i]->type);
        memset(name, 0, GPT_NAME_LEN / 2 + 1);
        utf16_to_cstring(name, reinterpret_cast<const uint16_t*>(a[i]->name), GPT_NAME_LEN / 2);

        printf("Name: %s \n  Start: %lu -- End: %lu \nType: %s\n",
               name, a[i]->first, a[i]->last, GUID);
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

int gpt_sync_current(int fd, uint64_t blocksize, gpt_header_t* header,
                     gpt_partition_t* ptable) {
    // write partition table first
    off_t rc = lseek(fd, header->entries * blocksize, SEEK_SET);
    if (rc < 0) {
        return -1;
    }
    size_t ptable_size = header->entries_count * header->entries_size;
    rc = write(fd, ptable, ptable_size);
    if (rc < 0 || rc != static_cast<off_t>(ptable_size)) {
        return -1;
    }
    // then write the header
    rc = lseek(fd, header->current * blocksize, SEEK_SET);
    if (rc < 0) {
        return -1;
    }

    uint8_t block[blocksize];
    memset(block, 0, sizeof(blocksize));
    memcpy(block, header, sizeof(*header));
    rc = write(fd, block, blocksize);
    if (rc != static_cast<off_t>(blocksize)) {
        return -1;
    }
    return 0;
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

} // namespace

__BEGIN_CDECLS

void gpt_set_debug_output_enabled(bool enabled) {
    debug_out = enabled;
}

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
    sprintf(dst, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid->data1, guid->data2, guid->data3, guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3], guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
}

const char* gpt_guid_to_type(const char* guid) {
    if (!strcmp("FE3A2A5D-4F32-41A7-B725-ACCC3285A309", guid)) {
        return "cros kernel";
    } else if (!strcmp("3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC", guid)) {
        return "cros rootfs";
    } else if (!strcmp("2E0A753D-9E48-43B0-8337-B15192CB1B5E", guid)) {
        return "cros reserved";
    } else if (!strcmp("CAB6E88E-ABF3-4102-A07A-D4BB9BE3C1D3", guid)) {
        return "cros firmware";
    } else if (!strcmp("C12A7328-F81F-11D2-BA4B-00A0C93EC93B", guid)) {
        return "efi system";
    } else if (!strcmp("EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", guid)) {
        return "data";
    } else if (!strcmp("21686148-6449-6E6F-744E-656564454649", guid)) {
        return "bios";
    } else if (!strcmp(GUID_SYSTEM_STRING, guid)) {
        return "fuchsia-system";
    } else if (!strcmp(GUID_DATA_STRING, guid)) {
        return "fuchsia-data";
    } else if (!strcmp(GUID_INSTALL_STRING, guid)) {
        return "fuchsia-install";
    } else if (!strcmp(GUID_BLOB_STRING, guid)) {
        return "fuchsia-blob";
    } else if (!strcmp(GUID_FVM_STRING, guid)) {
        return "fuchsia-fvm";
    } else if (!strcmp(GUID_ZIRCON_A_STRING, guid)) {
        return "zircon-a";
    } else if (!strcmp(GUID_ZIRCON_B_STRING, guid)) {
        return "zircon-b";
    } else if (!strcmp(GUID_ZIRCON_R_STRING, guid)) {
        return "zircon-r";
    } else if (!strcmp(GUID_SYS_CONFIG_STRING, guid)) {
        return "sys-config";
    } else if (!strcmp(GUID_FACTORY_CONFIG_STRING, guid)) {
        return "factory";
    } else if (!strcmp(GUID_BOOTLOADER_STRING, guid)) {
        return "bootloader";
    } else if (!strcmp(GUID_VBMETA_A_STRING, guid)) {
        return "vbmeta_a";
    } else if (!strcmp(GUID_VBMETA_B_STRING, guid)) {
        return "vbmeta_b";
    } else {
        return "unknown";
    }
}

__END_CDECLS

int GptDevice::FinalizeAndSync(bool persist) {

    // write fake mbr if needed
    uint8_t mbr[blocksize_];
    off_t rc;
    if (!mbr_) {
        memset(mbr, 0, blocksize_);
        mbr[0x1fe] = 0x55;
        mbr[0x1ff] = 0xaa;
        mbr_partition_t* mpart = reinterpret_cast<mbr_partition_t*>(mbr + 0x1be);
        mpart->chs_first[1] = 0x1;
        mpart->type = 0xee; // gpt protective mbr
        mpart->chs_last[0] = 0xfe;
        mpart->chs_last[1] = 0xff;
        mpart->chs_last[2] = 0xff;
        mpart->lba = 1;
        mpart->sectors = blocks_ & 0xffffffff;
        rc = lseek(fd_, 0, SEEK_SET);
        if (rc < 0) {
            return -1;
        }
        rc = write(fd_, mbr, blocksize_);
        if (rc < 0 || rc != static_cast<off_t>(blocksize_)) {
            return -1;
        }
        mbr_ = true;
    }

    // fill in the new header fields
    gpt_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = GPT_MAGIC;
    header.revision = 0x00010000; // gpt version 1.0
    header.size = GPT_HEADER_SIZE;
    if (valid_) {
        header.current = header_.current;
        header.backup = header_.backup;
        memcpy(header.guid, header_.guid, 16);
    } else {
        header.current = 1;
        // backup gpt is in the last block
        header.backup = blocks_ - 1;
        // generate a guid
        zx_cprng_draw(header.guid, GPT_GUID_LEN);
    }

    // always write 128 entries in partition table
    size_t ptable_size = kPartitionCount * sizeof(gpt_partition_t);
    gpt_partition_t* buf = static_cast<gpt_partition_t*>(malloc(ptable_size));
    if (!buf) {
        return -1;
    }
    memset(buf, 0, ptable_size);

    // generate partition table
    uint8_t* ptr = reinterpret_cast<uint8_t*>(buf);
    for (uint32_t i = 0; i < kPartitionCount && partitions_[i] != NULL; i++) {
        memcpy(ptr, partitions_[i], GPT_ENTRY_SIZE);
        ptr += GPT_ENTRY_SIZE;
    }

    // fill in partition table fields in header
    header.entries = valid_ ? header_.entries : 2;
    header.entries_count = kPartitionCount;
    header.entries_size = GPT_ENTRY_SIZE;
    header.entries_crc = crc32(0, reinterpret_cast<uint8_t*>(buf), ptable_size);

    uint64_t ptable_blocks = ptable_size / blocksize_;
    header.first = header.entries + ptable_blocks;
    header.last = header.backup - ptable_blocks - 1;

    // calculate header checksum
    header.crc32 = crc32(0, reinterpret_cast<const uint8_t*>(&header), GPT_HEADER_SIZE);

    // the copy cached in priv is the primary copy
    memcpy(&header_, &header, sizeof(header));

    // the header copy on stack is now the backup copy...
    header.current = header_.backup;
    header.backup = header_.current;
    header.entries = header_.last + 1;
    header.crc32 = 0;
    header.crc32 = crc32(0, reinterpret_cast<const uint8_t*>(&header), GPT_HEADER_SIZE);

    if (persist) {
        // write backup to disk
        rc = gpt_sync_current(fd_, blocksize_, &header, buf);
        if (rc < 0) {
            goto fail;
        }

        // write primary copy to disk
        rc = gpt_sync_current(fd_, blocksize_, &header_, buf);
        if (rc < 0) {
            goto fail;
        }
    }

    // align backup with new on-disk state
    memcpy(ptable_backup_, ptable_, sizeof(ptable_));

    valid_ = true;

    free(buf);
    return 0;
fail:
    free(buf);
    return -1;
}

void GptDevice::PrintTable() const {
    int count = 0;
    for (; partitions_[count] != NULL; ++count)
        ;
    print_array(partitions_, count);
}

int GptDevice::GetDiffs(uint32_t idx, uint32_t* diffs) const {

    *diffs = 0;

    if (idx >= kPartitionCount) {
        return -1;
    }

    if (partitions_[idx] == NULL) {
        return -1;
    }

    const gpt_partition_t* a = partitions_[idx];
    const gpt_partition_t* b = &ptable_backup_[idx];
    if (memcmp(a->type, b->type, sizeof(a->type))) {
        *diffs |= GPT_DIFF_TYPE;
    }
    if (memcmp(a->guid, b->guid, sizeof(a->guid))) {
        *diffs |= GPT_DIFF_GUID;
    }
    if (a->first != b->first) {
        *diffs |= GPT_DIFF_FIRST;
    }
    if (a->last != b->last) {
        *diffs |= GPT_DIFF_LAST;
    }
    if (a->flags != b->flags) {
        *diffs |= GPT_DIFF_FLAGS;
    }
    if (memcmp(a->name, b->name, sizeof(a->name))) {
        *diffs |= GPT_DIFF_NAME;
    }

    return 0;
}

zx_status_t GptDevice::Init(int fd, uint32_t blocksize, uint64_t blocks) {
    ssize_t ptable_size;
    uint32_t saved_crc, crc;
    gpt_partition_t* ptable;
    gpt_header_t* header;
    off_t rc;

    fd_ = fd;
    blocksize_ = blocksize;
    blocks_ = blocks;

    uint8_t block[blocksize];

    if (blocksize_ < 512) {
        G_PRINTF("blocksize < 512 not supported\n");
        return ZX_ERR_INTERNAL;
    }

    // Read protective MBR.
    rc = lseek(fd, 0, SEEK_SET);
    if (rc < 0) {
        return ZX_ERR_INTERNAL;
    }
    rc = read(fd, block, blocksize);
    if (rc < 0 || rc != static_cast<off_t>(blocksize)) {
        return ZX_ERR_INTERNAL;
    }
    mbr_ = block[0x1fe] == 0x55 && block[0x1ff] == 0xaa;

    // read the gpt header (lba 1)
    rc = read(fd, block, blocksize);
    if (rc < 0 || rc != static_cast<off_t>(blocksize)) {
        return ZX_ERR_INTERNAL;
    }

    header = &header_;
    memcpy(header, block, sizeof(*header));

    // is this a valid gpt header?
    if (header->magic != GPT_MAGIC) {
        G_PRINTF("invalid header magic!\n");
        // ok to have an invalid header
        return ZX_OK;
    }

    // header checksum
    saved_crc = header->crc32;
    header->crc32 = 0;
    crc = crc32(0, reinterpret_cast<const uint8_t*>(header), header->size);
    if (crc != saved_crc) {
        G_PRINTF("header crc check failed\n");
        return ZX_OK;
    }

    if (header->entries_count > kPartitionCount) {
        G_PRINTF("too many partitions!\n");
        return ZX_OK;
    }

    valid_ = true;

    if (header->entries_count == 0) {
        return ZX_OK;
    }
    if (header->entries_count > kPartitionCount) {
        G_PRINTF("too many partitions\n");
        return ZX_OK;
    }

    ptable = ptable_;

    // read the partition table
    rc = lseek(fd, header->entries * blocksize, SEEK_SET);
    if (rc < 0) {
        return ZX_ERR_INTERNAL;
    }
    ptable_size = header->entries_size * header->entries_count;
    if (static_cast<size_t>(ptable_size) > SIZE_MAX) {
        G_PRINTF("partition table too big\n");
        return ZX_OK;
    }
    rc = read(fd, ptable, ptable_size);
    if (rc != ptable_size) {
        return ZX_ERR_INTERNAL;
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
        if (ptable[i].first == 0 && ptable[i].last == 0) continue;
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

int GptDevice::Finalize() {
    return FinalizeAndSync(false);
}

int GptDevice::Sync() {
    return FinalizeAndSync(true);
}

int GptDevice::Range(uint64_t* block_start, uint64_t* block_end) const {

    if (!valid_) {
        G_PRINTF("partition header invalid\n");
        return -1;
    }

    // check range
    *block_start = header_.first;
    *block_end = header_.last;
    return 0;
}

int GptDevice::AddPartition(const char* name, const uint8_t* type,
                            const uint8_t* guid, uint64_t offset, uint64_t blocks,
                            uint64_t flags) {

    if (!valid_) {
        G_PRINTF("partition header invalid, sync to generate a default header\n");
        return -1;
    }

    if (blocks == 0) {
        G_PRINTF("partition must be at least 1 block\n");
        return -1;
    }

    uint64_t first = offset;
    uint64_t last = first + blocks - 1;

    // check range
    if (last < first || first < header_.first || last > header_.last) {
        G_PRINTF("partition must be in range of usable blocks[%" PRIu64 ", %" PRIu64 "]\n",
                 header_.first, header_.last);
        return -1;
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
            return -1;
        }
    }
    if (tail == -1) {
        G_PRINTF("too many partitions\n");
        return -1;
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
    return 0;
}

int GptDevice::ClearPartition(uint64_t offset, uint64_t blocks) {

    if (!valid_) {
        G_PRINTF("partition header invalid, sync to generate a default header\n");
        return -1;
    }

    if (blocks == 0) {
        G_PRINTF("must clear at least 1 block\n");
        return -1;
    }
    uint64_t first = offset;
    uint64_t last = offset + blocks - 1;

    if (last < first || first < header_.first || last > header_.last) {
        G_PRINTF("must clear in the range of usable blocks[%" PRIu64 ", %" PRIu64 "]\n",
                 header_.first, header_.last);
        return -1;
    }

    char zero[blocksize_];
    memset(zero, 0, sizeof(zero));

    for (size_t i = first; i <= last; i++) {
        if (pwrite(fd_, zero, sizeof(zero), blocksize_ * i) !=
            static_cast<ssize_t>(sizeof(zero))) {
            G_PRINTF("Failed to write to block %zu; errno: %d\n", i, errno);
            return -1;
        }
    }

    return 0;
}

int GptDevice::RemovePartition(const uint8_t* guid) {
    // look for the entry in the partition list
    uint32_t i;
    for (i = 0; i < kPartitionCount; i++) {
        if (!memcmp(partitions_[i]->guid, guid, sizeof(partitions_[i]->guid))) {
            break;
        }
    }
    if (i == kPartitionCount) {
        G_PRINTF("partition not found\n");
        return -1;
    }
    // clear the entry
    memset(partitions_[i], 0, GPT_ENTRY_SIZE);
    // pack the partition list
    for (i = i + 1; i < kPartitionCount; i++) {
        if (partitions_[i] == NULL) {
            partitions_[i - 1] = NULL;
        } else {
            partitions_[i - 1] = partitions_[i];
        }
    }
    return 0;
}

int GptDevice::RemoveAllPartitions() {
    memset(partitions_, 0, sizeof(partitions_));
    return 0;
}

void GptDevice::GetHeaderGuid(uint8_t (*disk_guid_out)[GPT_GUID_LEN]) const {
    memcpy(disk_guid_out, header_.guid, GPT_GUID_LEN);
}

} // namespace gpt
