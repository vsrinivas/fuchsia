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

static bool debug_out = false;

#define G_PRINTF(f, ...) if (debug_out) printf ((f), ##__VA_ARGS__);

void gpt_set_debug_output_enabled(bool enabled) {
    debug_out = enabled;
}

typedef struct mbr_partition {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba;
    uint32_t sectors;
} mbr_partition_t;

static_assert(sizeof(gpt_header_t) == GPT_HEADER_SIZE, "unexpected gpt header size");
static_assert(sizeof(gpt_partition_t) == GPT_ENTRY_SIZE, "unexpected gpt entry size");

typedef struct gpt_priv {
    // device to use
    int fd;

    // block size in bytes
    uint64_t blocksize;

    // number of blocks
    uint64_t blocks;

    // true if valid mbr exists on disk
    bool mbr;

    // header buffer, should be primary copy
    gpt_header_t header;

    // partition table buffer
    gpt_partition_t ptable[PARTITIONS_COUNT];

    // copy of buffer from when last init'd or sync'd.
    gpt_partition_t backup[PARTITIONS_COUNT];

    gpt_device_t device;
} gpt_priv_t;

#define get_priv(dev) ((gpt_priv_t*)((uintptr_t)(dev)-offsetof(gpt_priv_t, device)))

void cstring_to_utf16(uint16_t* dst, const char* src, size_t maxlen) {
    size_t len = strlen(src);
    if (len > maxlen) len = maxlen;
    for (size_t i = 0; i < len; i++) {
        *dst++ = (uint16_t)(*src++ & 0x7f);
    }
}

char* utf16_to_cstring(char* dst, const uint16_t* src, size_t len) {
    size_t i = 0;
    char* ptr = dst;
    while (i < len) {
        char c = src[i++] & 0x7f;
        if (!c) continue;
        *ptr++ = c;
    }
    return dst;
}

static void print_array(gpt_partition_t** a, int c) {
    char GUID[GPT_GUID_STRLEN];
    char name[GPT_NAME_LEN / 2 + 1];

    for (int i = 0; i < c; ++i) {
        uint8_to_guid_string(GUID, a[i]->type);
        memset(name, 0, GPT_NAME_LEN / 2 + 1);
        utf16_to_cstring(name, (const uint16_t*)a[i]->name, GPT_NAME_LEN / 2);

        printf("Name: %s \n  Start: %lu -- End: %lu \nType: %s\n",
               name, a[i]->first, a[i]->last, GUID);
    }
}

static void partition_init(gpt_partition_t* part, const char* name, uint8_t* type, uint8_t* guid,
                           uint64_t first, uint64_t last, uint64_t flags) {
    memcpy(part->type, type, sizeof(part->type));
    memcpy(part->guid, guid, sizeof(part->guid));
    part->first = first;
    part->last = last;
    part->flags = flags;
    cstring_to_utf16((uint16_t*)part->name, name, sizeof(part->name) / sizeof(uint16_t));
}

static uint32_t GPT_RESERVED = 16 * 1024;

uint32_t gpt_device_get_size_blocks(uint32_t block_sz) {
    return ((uint32_t) howmany(GPT_RESERVED, block_sz)) + 2;
}

void print_table(gpt_device_t* device) {
    int count = 0;
    for (; device->partitions[count] != NULL; ++count)
        ;
    print_array(device->partitions, count);
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

int gpt_get_diffs(gpt_device_t* dev, int idx, unsigned* diffs) {
    gpt_priv_t* priv = get_priv(dev);

    *diffs = 0;

    if (idx >= PARTITIONS_COUNT) {
        return -1;
    }

    if (dev->partitions[idx] == NULL) {
        return -1;
    }

    gpt_partition_t* a = dev->partitions[idx];
    gpt_partition_t* b = priv->backup + idx;
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

int gpt_device_init(int fd, uint64_t blocksize, uint64_t blocks, gpt_device_t** out_dev) {
    gpt_priv_t* priv = calloc(1, sizeof(gpt_priv_t));
    if (!priv) return -1;

    priv->fd = fd;
    priv->blocksize = blocksize;
    priv->blocks = blocks;

    uint8_t block[blocksize];

    if (priv->blocksize < 512) {
        G_PRINTF("blocksize < 512 not supported\n");
        goto fail;
    }

    // Read protective MBR.
    int rc = lseek(fd, 0, SEEK_SET);
    if (rc < 0) {
        goto fail;
    }
    rc = read(fd, block, blocksize);
    if (rc < 0 || (uint64_t)rc != blocksize) {
        goto fail;
    }
    priv->mbr = block[0x1fe] == 0x55 && block[0x1ff] == 0xaa;

    // read the gpt header (lba 1)
    rc = read(fd, block, blocksize);
    if (rc < 0 || (uint64_t)rc != blocksize) {
        goto fail;
    }

    gpt_header_t* header = &priv->header;
    memcpy(header, block, sizeof(*header));

    // is this a valid gpt header?
    if (header->magic != GPT_MAGIC) {
        G_PRINTF("invalid header magic!\n");
        goto out; // ok to have an invalid header
    }

    // header checksum
    uint32_t saved_crc = header->crc32;
    header->crc32 = 0;
    uint32_t crc = crc32(0, (const unsigned char*)header, header->size);
    if (crc != saved_crc) {
        G_PRINTF("header crc check failed\n");
        goto out;
    }

    if (header->entries_count > PARTITIONS_COUNT) {
        G_PRINTF("too many partitions!\n");
        goto out;
    }

    priv->device.valid = true;

    if (header->entries_count == 0) {
        goto out;
    }
    if (header->entries_count > PARTITIONS_COUNT) {
        G_PRINTF("too many partitions\n");
        goto out;
    }

    gpt_partition_t* ptable = priv->ptable;

    // read the partition table
    rc = lseek(fd, header->entries * blocksize, SEEK_SET);
    if (rc < 0) {
        goto fail;
    }
    ssize_t ptable_size = header->entries_size * header->entries_count;
    if ((size_t)ptable_size > SIZE_MAX) {
        G_PRINTF("partition table too big\n");
        goto out;
    }
    rc = read(fd, ptable, ptable_size);
    if (rc != ptable_size) {
        goto fail;
    }

    // partition table checksum
    crc = crc32(0, (const unsigned char*)ptable, ptable_size);
    if (crc != header->entries_crc) {
        G_PRINTF("table crc check failed\n");
        goto out;
    }

    // save original state so we can know what we changed
    memcpy(priv->backup, priv->ptable, sizeof(priv->ptable));

    // fill the table of valid partitions
    for (unsigned i = 0; i < header->entries_count; i++) {
        if (ptable[i].first == 0 && ptable[i].last == 0) continue;
        priv->device.partitions[i] = &ptable[i];
    }
out:
    *out_dev = &priv->device;
    return 0;
fail:
    free(priv);
    return -1;
}

void gpt_device_release(gpt_device_t* dev) {
    gpt_priv_t* priv = get_priv(dev);
    free(priv);
}

static int gpt_sync_current(int fd, uint64_t blocksize, gpt_header_t* header,
                            gpt_partition_t* ptable) {
    // write partition table first
    ssize_t rc = lseek(fd, header->entries * blocksize, SEEK_SET);
    if (rc < 0) {
        return -1;
    }
    size_t ptable_size = header->entries_count * header->entries_size;
    rc = write(fd, ptable, ptable_size);
    if (rc < 0 || (size_t)rc != ptable_size) {
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
    if (rc != (ssize_t) blocksize) {
        return -1;
    }
    return 0;
}

static int gpt_device_finalize_and_sync(gpt_device_t* dev, bool persist) {
    gpt_priv_t* priv = get_priv(dev);

    // write fake mbr if needed
    uint8_t mbr[priv->blocksize];
    int rc;
    if (!priv->mbr) {
        memset(mbr, 0, priv->blocksize);
        mbr[0x1fe] = 0x55;
        mbr[0x1ff] = 0xaa;
        mbr_partition_t* mpart = (mbr_partition_t*)(mbr + 0x1be);
        mpart->chs_first[1] = 0x1;
        mpart->type = 0xee; // gpt protective mbr
        mpart->chs_last[0] = 0xfe;
        mpart->chs_last[1] = 0xff;
        mpart->chs_last[2] = 0xff;
        mpart->lba = 1;
        mpart->sectors = priv->blocks & 0xffffffff;
        rc = lseek(priv->fd, 0, SEEK_SET);
        if (rc < 0) {
            return -1;
        }
        rc = write(priv->fd, mbr, priv->blocksize);
        if (rc < 0 || (size_t)rc != priv->blocksize) {
            return -1;
        }
        priv->mbr = true;
    }

    // fill in the new header fields
    gpt_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = GPT_MAGIC;
    header.revision = 0x00010000; // gpt version 1.0
    header.size = GPT_HEADER_SIZE;
    if (dev->valid) {
        header.current = priv->header.current;
        header.backup = priv->header.backup;
        memcpy(header.guid, priv->header.guid, 16);
    } else {
        header.current = 1;
        // backup gpt is in the last block
        header.backup = priv->blocks - 1;
        // generate a guid
        zx_cprng_draw(header.guid, GPT_GUID_LEN);
    }

    // always write 128 entries in partition table
    size_t ptable_size = PARTITIONS_COUNT * sizeof(gpt_partition_t);
    void* buf = malloc(ptable_size);
    if (!buf) {
        return -1;
    }
    memset(buf, 0, ptable_size);

    // generate partition table
    void* ptr = buf;
    int i;
    gpt_partition_t** p;
    for (i = 0, p = dev->partitions; i < PARTITIONS_COUNT && *p; i++, p++) {
        memcpy(ptr, *p, GPT_ENTRY_SIZE);
        ptr += GPT_ENTRY_SIZE;
    }

    // fill in partition table fields in header
    header.entries = dev->valid ? priv->header.entries : 2;
    header.entries_count = PARTITIONS_COUNT;
    header.entries_size = GPT_ENTRY_SIZE;
    header.entries_crc = crc32(0, buf, ptable_size);

    uint64_t ptable_blocks = ptable_size / priv->blocksize;
    header.first = header.entries + ptable_blocks;
    header.last = header.backup - ptable_blocks - 1;

    // calculate header checksum
    header.crc32 = crc32(0, (const unsigned char*)&header, GPT_HEADER_SIZE);

    // the copy cached in priv is the primary copy
    memcpy(&priv->header, &header, sizeof(header));

    // the header copy on stack is now the backup copy...
    header.current = priv->header.backup;
    header.backup = priv->header.current;
    header.entries = priv->header.last + 1;
    header.crc32 = 0;
    header.crc32 = crc32(0, (const unsigned char*)&header, GPT_HEADER_SIZE);

    if (persist) {
        // write backup to disk
        rc = gpt_sync_current(priv->fd, priv->blocksize, &header, buf);
        if (rc < 0) {
            goto fail;
        }

        // write primary copy to disk
        rc = gpt_sync_current(priv->fd, priv->blocksize, &priv->header, buf);
        if (rc < 0) {
            goto fail;
        }
    }

    // align backup with new on-disk state
    memcpy(priv->backup, priv->ptable, sizeof(priv->ptable));

    dev->valid = true;

    free(buf);
    return 0;
fail:
    free(buf);
    return -1;
}

int gpt_device_finalize(gpt_device_t* dev) {
    return gpt_device_finalize_and_sync(dev, false);
}

int gpt_device_sync(gpt_device_t* dev) {
    return gpt_device_finalize_and_sync(dev, true);
}

int gpt_device_range(gpt_device_t* dev, uint64_t* block_start, uint64_t* block_end) {
    gpt_priv_t* priv = get_priv(dev);

    if (!dev->valid) {
        G_PRINTF("partition header invalid\n");
        return -1;
    }

    // check range
    *block_start = priv->header.first;
    *block_end = priv->header.last;
    return 0;
}

int gpt_partition_add(gpt_device_t* dev, const char* name, uint8_t* type, uint8_t* guid,
                      uint64_t offset, uint64_t blocks, uint64_t flags) {
    gpt_priv_t* priv = get_priv(dev);

    if (!dev->valid) {
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
    if (last < first || first < priv->header.first || last > priv->header.last) {
        G_PRINTF("partition must be in range of usable blocks[%" PRIu64", %" PRIu64"]\n",
                 priv->header.first, priv->header.last);
        return -1;
    }

    // check for overlap
    int i;
    int tail = -1;
    for (i = 0; i < PARTITIONS_COUNT; i++) {
        if (!dev->partitions[i]) {
            tail = i;
            break;
        }
        if (first <= dev->partitions[i]->last && last >= dev->partitions[i]->first) {
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
    for (i = 0; i < PARTITIONS_COUNT; i++) {
        if (priv->ptable[i].first == 0 && priv->ptable[i].last == 0) {
            part = &priv->ptable[i];
            break;
        }
    }
    assert(part);

    // insert the new element into the list
    partition_init(part, name, type, guid, first, last, flags);
    dev->partitions[tail] = part;
    return 0;
}

int gpt_partition_clear(gpt_device_t* dev, uint64_t offset, uint64_t blocks) {
    gpt_priv_t* priv = get_priv(dev);

    if (!dev->valid) {
        G_PRINTF("partition header invalid, sync to generate a default header\n");
        return -1;
    }

    if (blocks == 0) {
        G_PRINTF("must clear at least 1 block\n");
        return -1;
    }
    uint64_t first = offset;
    uint64_t last = offset + blocks - 1;

    if (last < first || first < priv->header.first || last > priv->header.last) {
        G_PRINTF("must clear in the range of usable blocks[%" PRIu64", %" PRIu64"]\n",
                 priv->header.first, priv->header.last);
        return -1;
    }

    char zero[priv->blocksize];
    memset(zero, 0, sizeof(zero));

    for (size_t i = first; i <= last; i++) {
        if (pwrite(priv->fd, zero, sizeof(zero), priv->blocksize * i) !=
            (ssize_t) sizeof(zero)) {
            G_PRINTF("Failed to write to block %zu; errno: %d\n", i, errno);
            return -1;
        }
    }

    return 0;
}

int gpt_partition_remove(gpt_device_t* dev, const uint8_t* guid) {
    // look for the entry in the partition list
    int i;
    for (i = 0; i < PARTITIONS_COUNT; i++) {
        if (!memcmp(dev->partitions[i]->guid, guid, sizeof(dev->partitions[i]->guid))) {
            break;
        }
    }
    if (i == PARTITIONS_COUNT) {
        G_PRINTF("partition not found\n");
        return -1;
    }
    // clear the entry
    memset(dev->partitions[i], 0, GPT_ENTRY_SIZE);
    // pack the partition list
    for (i = i + 1; i < PARTITIONS_COUNT; i++) {
        if (dev->partitions[i] == NULL) {
            dev->partitions[i-1] = NULL;
        } else {
            dev->partitions[i-1] = dev->partitions[i];
        }
    }
    return 0;
}

int gpt_partition_remove_all(gpt_device_t* dev) {
    memset(dev->partitions, 0, sizeof(dev->partitions));
    return 0;
}

// Since the human-readable representation of a GUID is the following format,
// ordered little-endian, it is useful to group a GUID into these
// appropriately-sized groups.
struct guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

void uint8_to_guid_string(char* dst, const uint8_t* src) {
    struct guid* guid = (struct guid*)src;
    sprintf(dst, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid->data1, guid->data2, guid->data3, guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3], guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
}

void gpt_device_get_header_guid(gpt_device_t* dev,
                                uint8_t (*disk_guid_out)[GPT_GUID_LEN]) {
    gpt_header_t* header = &get_priv(dev)->header;
    memcpy(disk_guid_out, header->guid, GPT_GUID_LEN);
}

int gpt_device_read_gpt(int fd, gpt_device_t** gpt_out) {
    block_info_t info;

    ssize_t rc = ioctl_block_get_info(fd, &info);
    if (rc < 0) {
        return rc;
    }

    if (info.block_size < 1) {
        return -1;
    }

    int result = gpt_device_init(fd, info.block_size, info.block_count,
                                 gpt_out);

    if (result < 0) {
        return result;
    } else if (!(*gpt_out)->valid) {
        gpt_device_release(*gpt_out);
        *gpt_out = NULL;
        return -1;
    }

    return 0;
}

static int compare(const void *ls, const void *rs) {
    if ((*(gpt_partition_t **)ls)->first > (*(gpt_partition_t **)rs)->first) {
        return 1;
    } else if ((*(gpt_partition_t **)ls)->first < (*(gpt_partition_t **)rs)->first) {
        return -1;
    } else {
        return 0;
    }
}

void gpt_sort_partitions(gpt_partition_t** in, gpt_partition_t** sorted_out,
                         uint16_t count) {

    for (uint16_t idx = 0; idx < count; idx++) {
        sorted_out[idx] = in[idx];
    }
    qsort(sorted_out, count, sizeof(gpt_partition_t*), compare);
}
