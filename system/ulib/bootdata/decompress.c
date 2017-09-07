// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bootdata/decompress.h>

#include <limits.h>
#include <string.h>

#include <magenta/boot/bootdata.h>
#include <magenta/compiler.h>
#include <magenta/syscalls.h>

#include <lz4/lz4.h>

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

static mx_status_t check_lz4_frame(const lz4_frame_desc* fd,
                                   size_t expected, const char** err) {
    if ((fd->flag & MX_LZ4_FLAG_VERSION) != MX_LZ4_VERSION) {
        *err = "bad lz4 version for bootfs";
        return MX_ERR_INVALID_ARGS;
    }
    if ((fd->flag & MX_LZ4_FLAG_BLOCK_DEP) == 0) {
        *err = "bad lz4 flag (blocks must be independent)";
        return MX_ERR_INVALID_ARGS;
    }
    if (fd->flag & MX_LZ4_FLAG_BLOCK_CKSUM) {
        *err = "bad lz4 flag (block checksum must be disabled)";
        return MX_ERR_INVALID_ARGS;
    }
    if ((fd->flag & MX_LZ4_FLAG_CONTENT_SZ) == 0) {
        *err = "bad lz4 flag (content size must be included)";
        return MX_ERR_INVALID_ARGS;
    }
    if (fd->flag & MX_LZ4_FLAG_RESERVED) {
        *err = "bad lz4 flag (reserved bits in flg must be zero)";
        return MX_ERR_INVALID_ARGS;
    }
    if ((fd->block_desc & MX_LZ4_BLOCK_MAX_MASK) != MX_LZ4_BLOCK_64KB) {
        *err = "bad lz4 flag (max block size must be 64k)";
        return MX_ERR_INVALID_ARGS;
    }
    if (fd->block_desc & ~MX_LZ4_BLOCK_MAX_MASK) {
        *err = "bad lz4 flag (reserved bits in bd must be zero)";
        return MX_ERR_INVALID_ARGS;
    }
    if (fd->content_size != expected) {
        *err = "lz4 content size does not match bootdata outsize";
        return MX_ERR_INVALID_ARGS;
    }

    // TODO: header checksum
    return MX_OK;
}

static mx_status_t decompress_bootfs_vmo(mx_handle_t vmar, const uint8_t* data,
                                         size_t _outsize, mx_handle_t* out,
                                         const char** err) {
    if (*(const uint32_t*)data != MX_LZ4_MAGIC) {
        *err = "bad magic number for compressed bootfs";
        return MX_ERR_INVALID_ARGS;
    }
    data += sizeof(uint32_t);

    check_lz4_frame((const lz4_frame_desc*)data, _outsize, err);
    data += sizeof(lz4_frame_desc);

    size_t outsize = (_outsize + 4095) & ~4095;
    if (outsize < _outsize) {
        // newsize wrapped, which means the outsize was too large
        *err = "lz4 output size too large";
        return MX_ERR_NO_MEMORY;
    }
    mx_handle_t dst_vmo;
    mx_status_t status = mx_vmo_create((uint64_t)outsize, 0, &dst_vmo);
    if (status < 0) {
        *err = "mx_vmo_create failed for decompressing bootfs";
        return status;
    }
    mx_object_set_property(dst_vmo, MX_PROP_NAME, "bootfs", 6);

    uintptr_t dst_addr = 0;
    status = mx_vmar_map(vmar, 0, dst_vmo, 0, outsize,
            MX_VM_FLAG_PERM_READ|MX_VM_FLAG_PERM_WRITE, &dst_addr);
    if (status < 0) {
        *err = "mx_vmar_map failed on bootfs vmo during decompression";
        return status;
    }

    size_t remaining = outsize;
    uint8_t* dst = (uint8_t*)dst_addr;

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
                *err = "bootdata outsize too small for lz4 decompression";
                return MX_ERR_INVALID_ARGS;
            }
            remaining -= actual;
        } else {
            int dcmp = LZ4_decompress_safe((const char*)data, (char*)dst, blocksize, remaining);
            if (dcmp < 0) {
                *err = "lz4 decompression failed";
                return MX_ERR_BAD_STATE;
            }
            dst += dcmp;
            data += blocksize;
            if (remaining - dcmp > remaining) {
                // Remaining wrapped around (would be negative if signed)
                *err = "bootdata outsize too small for lz4 decompression";
                return MX_ERR_INVALID_ARGS;
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
        *err = "bootdata size error; outsize does not match decompressed size";
        return MX_ERR_INVALID_ARGS;
    }

    status = mx_vmar_unmap(vmar, dst_addr, outsize);
    if (status < 0) {
        *err = "mx_vmar_unmap after decompress failed";
        return status;
    }
    *out = dst_vmo;
    return MX_OK;
}

mx_status_t decompress_bootdata(mx_handle_t vmar, mx_handle_t vmo,
                                size_t offset, size_t length,
                                mx_handle_t* out, const char** err) {
    *err = "none";

    if (length > SIZE_MAX) {
        *err = "bootfs VMO too large to map";
        return MX_ERR_BUFFER_TOO_SMALL;
    }

    uintptr_t addr = 0;
    size_t aligned_offset = offset & ~(PAGE_SIZE - 1);
    size_t align_shift = offset - aligned_offset;
    length += align_shift;
    mx_status_t status = mx_vmar_map(vmar, 0, vmo, aligned_offset, length, MX_VM_FLAG_PERM_READ, &addr);
    if (status < 0) {
        *err = "mx_vmar_map failed on bootfs vmo";
        return status;
    }
    uintptr_t bootdata_addr = addr + align_shift;

    const bootdata_t* hdr = (bootdata_t*)bootdata_addr;
    bootdata_addr += sizeof(bootdata_t);
    if (hdr->flags & BOOTDATA_FLAG_EXTRA)
        bootdata_addr += sizeof(bootextra_t);

    switch (hdr->type) {
    case BOOTDATA_BOOTFS_BOOT:
    case BOOTDATA_BOOTFS_SYSTEM:
        if (hdr->flags & BOOTDATA_BOOTFS_FLAG_COMPRESSED) {
            status = decompress_bootfs_vmo(vmar, (const uint8_t*)bootdata_addr, hdr->extra, out, err);
        }
        break;
    default:
        *err = "unknown bootdata type, not attempting decompression\n";
        status = MX_ERR_NOT_SUPPORTED;
        break;
    }

    mx_status_t s = mx_vmar_unmap(vmar, addr, length);
    if (s < 0) {
        *err = "mx_vmar_unmap failed on bootfs vmo";
        return s;
    }

    return status;
}
