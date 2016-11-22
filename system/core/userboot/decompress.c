// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "decompress.h"
#include "util.h"

#include <string.h>

#include <magenta/bootdata.h>
#include <magenta/compiler.h>
#include <magenta/syscalls.h>

#include <lz4.h>

// The LZ4 Frame format is used to compress a bootfs image, but we cannot use
// the LZ4 library's decompression functions in userboot. The following
// definitions are used in the reimplementation of LZ4 Frame decompression, with
// a few restrictions on the frame options:
//  - Blocks must be independent
//  - No block checksums
//  - Final content size must be included in frame header
//  - Max block size is 64kB
//
//  See https://github.com/lz4/lz4/blob/dev/lz4_Frame_format.md for details.
#define MX_LZ4_MAGIC 0x184D2204
#define MX_LZ4_VERSION (1 << 6)

typedef struct {
    uint8_t flag;
    uint8_t block_desc;
    uint64_t content_size;
    uint8_t header_cksum;
} __PACKED lz4_frame_desc;

#define MX_LZ4_FLAG_VERSION       (1 << 6)
#define MX_LZ4_FLAG_BLOCK_DEP     (1 << 5)
#define MX_LZ4_FLAG_BLOCK_CKSUM   (1 << 4)
#define MX_LZ4_FLAG_CONTENT_SZ    (1 << 3)
#define MX_LZ4_FLAG_CONTENT_CKSUM (1 << 2)
#define MX_LZ4_FLAG_RESERVED      0x03

#define MX_LZ4_BLOCK_MAX_MASK     (7 << 4)
#define MX_LZ4_BLOCK_64KB         (4 << 4)
#define MX_LZ4_BLOCK_256KB        (5 << 4)
#define MX_LZ4_BLOCK_1MB          (6 << 4)
#define MX_LZ4_BLOCK_4MB          (7 << 4)

static void check_lz4_frame(mx_handle_t log, const lz4_frame_desc* fd, size_t expected) {
    if ((fd->flag & MX_LZ4_FLAG_VERSION) != MX_LZ4_VERSION) {
        fail(log, ERR_INVALID_ARGS, "bad lz4 version for bootfs\n");
    }
    if ((fd->flag & MX_LZ4_FLAG_BLOCK_DEP) == 0) {
        fail(log, ERR_INVALID_ARGS, "bad lz4 flag (blocks must be independent)\n");
    }
    if (fd->flag & MX_LZ4_FLAG_BLOCK_CKSUM) {
        fail(log, ERR_INVALID_ARGS, "bad lz4 flag (block checksum must be disabled)\n");
    }
    if ((fd->flag & MX_LZ4_FLAG_CONTENT_SZ) == 0) {
        fail(log, ERR_INVALID_ARGS, "bad lz4 flag (content size must be included)\n");
    }
    if (fd->flag & MX_LZ4_FLAG_RESERVED) {
        fail(log, ERR_INVALID_ARGS, "bad lz4 flag (reserved bits in flg must be zero)\n");
    }

    if ((fd->block_desc & MX_LZ4_BLOCK_MAX_MASK) != MX_LZ4_BLOCK_64KB) {
        fail(log, ERR_INVALID_ARGS, "bad lz4 flag (max block size must be 64k)\n");
    }
    if (fd->block_desc & ~MX_LZ4_BLOCK_MAX_MASK) {
        fail(log, ERR_INVALID_ARGS, "bad lz4 flag (reserved bits in bd must be zero)\n");
    }

    if (fd->content_size != expected) {
        fail(log, ERR_INVALID_ARGS, "lz4 content size does not match bootdata outsize\n");
    }

    // TODO: header checksum
}

static mx_handle_t decompress_bootfs_vmo(mx_handle_t log, mx_handle_t vmar, const uint8_t* data) {
    const bootdata_t* hdr = (bootdata_t*)data;

    // Skip past the bootdata header
    data += sizeof(bootdata_t);

    if (*(const uint32_t*)data != MX_LZ4_MAGIC) {
        fail(log, ERR_INVALID_ARGS, "bad magic number for compressed bootfs\n");
    }
    data += sizeof(uint32_t);

    size_t newsize = hdr->outsize;
    check_lz4_frame(log, (const lz4_frame_desc*)data, newsize - sizeof(bootdata_t));
    data += sizeof(lz4_frame_desc);

    newsize = (newsize + 4095) & ~4095;
    if (newsize < hdr->outsize) {
        // newsize wrapped, which means the outsize was too large
        fail(log, ERR_NO_MEMORY, "lz4 output size too large\n");
    }
    mx_handle_t dst_vmo;
    mx_status_t status = mx_vmo_create((uint64_t)newsize, 0, &dst_vmo);
    if (status < 0) {
        check(log, status, "mx_vmo_create failed for decompressing bootfs\n");
    }

    uintptr_t dst_addr = 0;
    status = mx_vmar_map(vmar, 0, dst_vmo, 0, newsize,
            MX_VM_FLAG_PERM_READ|MX_VM_FLAG_PERM_WRITE, &dst_addr);
    check(log, status, "mx_vmar_map failed on bootfs vmo during decompression\n");

    size_t remaining = newsize;
    uint8_t* dst = (uint8_t*)dst_addr;

    bootdata_t* boothdr = (bootdata_t*)dst;
    // Copy the bootdata header but mark it as not compressed
    *boothdr = *hdr;
    boothdr->insize = hdr->outsize;
    boothdr->flags &= ~BOOTDATA_BOOTFS_FLAG_COMPRESSED;
    dst += sizeof(bootdata_t);
    remaining -= sizeof(bootdata_t);

    // Read each LZ4 block and decompress it. Block sizes are 32 bits.
    uint32_t blocksize = *(const uint32_t*)data;
    data += sizeof(uint32_t);
    while (blocksize) {
        // If the data is uncompressed, the high bit is 1.
        if (blocksize >> 31) {
            uint32_t actual = blocksize & 0x7fffffff;
            memcpy(dst, data, actual);
            dst += actual;
            data += actual;
            if (remaining - actual > remaining) {
                // Remaining wrapped around (would be negative if signed)
                fail(log, ERR_INVALID_ARGS, "bootdata outsize too small for lz4 decompression\n");
            }
            remaining -= actual;
        } else {
            int dcmp = LZ4_decompress_safe((const char*)data, (char*)dst, blocksize, remaining);
            if (dcmp < 0) {
                fail(log, ERR_BAD_STATE, "lz4 decompression failed\n");
            }
            dst += dcmp;
            data += blocksize;
            if (remaining - dcmp > remaining) {
                // Remaining wrapped around (would be negative if signed)
                fail(log, ERR_INVALID_ARGS, "bootdata outsize too small for lz4 decompression\n");
            }
            remaining -= dcmp;
        }

        blocksize = *(uint32_t*)data;
        data += sizeof(uint32_t);
    }

    // Sanity check: verify that we didn't have more than one page leftover.
    // The bootdata header should have specified the exact outsize needed, which
    // we rounded up to the next full page.
    if (remaining > 4095) {
        fail(log, ERR_INVALID_ARGS,
                "bootdata size error; outsize does not match decompressed size\n");
    }

    status = mx_vmar_unmap(vmar, dst_addr, newsize);
    check(log, status, "mx_vmar_unmap after decompress failed\n");
    return dst_vmo;
}

mx_handle_t decompress_vmo(mx_handle_t log, mx_handle_t vmar, mx_handle_t vmo) {
    uint64_t size;
    mx_status_t status = mx_vmo_get_size(vmo, &size);
    check(log, status, "mx_vmo_get_size failed on bootfs vmo\n");
    if (size < sizeof(bootdata_t)) {
        // If the vmo is too small to contain bootdata (e.g., an empty ramdisk),
        // just return it.
        return vmo;
    }
    if (size > SIZE_MAX) {
        fail(log, ERR_BUFFER_TOO_SMALL, "bootfs VMO too large to map\n");
    }

    uintptr_t addr = 0;
    status = mx_vmar_map(vmar, 0, vmo, 0, (size_t)size, MX_VM_FLAG_PERM_READ, &addr);
    check(log, status, "mx_vmar_map failed on bootfs vmo\n");

    const bootdata_t* hdr = (bootdata_t*)addr;
    if (hdr->magic != BOOTDATA_MAGIC) {
        fail(log, ERR_INVALID_ARGS, "bad boot data header\n");
    }

    mx_handle_t ret = vmo;
    switch (hdr->type) {
    case BOOTDATA_TYPE_BOOTFS:
        if (hdr->flags & BOOTDATA_BOOTFS_FLAG_COMPRESSED) {
            mx_handle_t newvmo = decompress_bootfs_vmo(log, vmar, (const uint8_t*)addr);
            mx_handle_close(vmo);
            ret = newvmo;
        }
        break;
    default:
        print(log, "unknown bootdata type, not attempting decompression\n", NULL);
        break;
    }
    status = mx_vmar_unmap(vmar, addr, size);
    check(log, status, "mx_vmar_unmap failed\n");

    return ret;
}
