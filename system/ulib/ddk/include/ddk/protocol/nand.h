// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stdint.h>

#include <zircon/types.h>
#include <zircon/boot/image.h>

// nand_info_t is used to retrieve various parameters describing the geometry of
// the underlying NAND chip(s). This is retrieved using the query api in
// nand_protocol_ops.
typedef struct nand_info nand_info_t;

enum {
    NAND_CLASS_PARTMAP = 1,   // NAND device contains multiple partitions.
    NAND_CLASS_FTL = 2,       // NAND device is a FTL partition.
    NAND_CLASS_BBS = 3,       // NAND device is a bad block skip partition.
};

struct nand_info {
    uint32_t page_size;         // Read/write unit size, in bytes.
    uint32_t pages_per_block;   // Erase block size, in pages.
    uint32_t num_blocks;        // Device size, in erase blocks.
    uint32_t ecc_bits;          // Number of ECC bits (correctable bit flips),
                                // per correction chunk.
    uint32_t oob_size;          // Available out of band bytes per page.
    uint32_t nand_class;        // NAND_CLASS_PARTMAP, NAND_CLASS_FTL or NAND_CLASS_RAW.
    uint8_t partition_guid[ZBI_PARTITION_GUID_LEN]; // partition type GUID from partition map.
};

// nand_op_t's are submitted for processing via the queue() method of the
// nand_protocol. Once submitted, the contents of the nand_op_t may be modified
// while it's being processed.
//
// The completion_cb() must eventually be called upon success or failure and
// at that point the cookie field must contain whatever value was in it when
// the nand_op_t was originally queued.
//
// Any mention of "in pages" in this file means nand pages, as reported by
// nand_info.page_size, as opposed to physical memory pages (RAM). That's true
// even for vmo-related values.
//
// corrected_bit_flips are always related to nand_info.ecc_bits, so it is
// possible to obtain a value that is larger than what is being read (in the oob
// case). On the other hand, if errors cannot be corrected, the operation will
// fail, and corrected_bit_flips will be undefined.

// NOTE: The protocol can be extended with barriers to support controllers that
// may issue multiple simultaneous request to the IO chips.


#define NAND_OP_READ                    0x00000001
#define NAND_OP_WRITE                   0x00000002
#define NAND_OP_ERASE                   0x00000003

// These operations are deprecated. Don't use for new code. See ZX-2233.
#define NAND_OP_READ_DATA               0x00000001
#define NAND_OP_WRITE_DATA              0x00000002
#define NAND_OP_READ_OOB                0x00000004
#define NAND_OP_WRITE_OOB               0x00000005

#define NAND_OP_READ_PAGE_DATA_OOB      0x00000006      // Read exactly 1 page data+oob.
#define NAND_OP_WRITE_PAGE_DATA_OOB     0x00000007      // Write exactly 1 page data+oob
                                                        // one of data/oob can be absent.
                                                        // Allows for data+oob to be updated
                                                        // in 1 IO

typedef struct nand_op nand_op_t;

struct nand_op {
    union {
        // All Commands.
        uint32_t command;                // Command.

        // NAND_OP_READ, NAND_OP_WRITE.
        //
        // A single operation can read or write an arbitrary number of pages,
        // including out of band (OOB) data for each page. If either regular
        // data or OOB is not required, the relevant VMO handle should be set to
        // ZX_HANDLE_INVALID.
        //
        // Note that length dictates the number of pages to access, regardless
        // of the type of data requested: regular data, OOB or both.
        //
        // The OOB data will be copied to (and from) a contiguous memory range
        // starting at the given offset. Note that said offset is given in nand
        // pages even though OOB is just a handful of bytes per page. In other
        // words, after said offset, the OOB data for each page is located
        // nand_info.oob_size bytes apart.
        //
        // For example, to read 5 pages worth of data + OOB, with page size of
        // 2 kB and 16 bytes of OOB per page, setting:
        //
        //     data_vmo = oob_vmo = vmo_handle
        //     length = 5
        //     offset_nand = 20
        //     offset_data_vmo = 0
        //     offset_oob_vmo = 5
        //
        // will transfer pages [20, 24] to the first 2048 * 5 bytes of the vmo,
        // followed by 16 * 5 bytes of OOB data starting at offset 2048 * 5.
        //
        struct {
            uint32_t command;            // Command.
            zx_handle_t data_vmo;        // vmo of data to read or write.
            zx_handle_t oob_vmo;         // vmo of OOB data to read or write.
            uint32_t length;             // Number of pages to access.
                                         // (0 is invalid).
            uint32_t offset_nand;        // Offset into nand, in pages.
            uint64_t offset_data_vmo;    // Data vmo offset in (nand) pages.
            uint64_t offset_oob_vmo;     // OOB vmo offset in (nand) pages.
            uint64_t* pages;             // Optional physical page list.
            // Return value from READ_DATA, max corrected bit flips in any
            // underlying ECC chunk read. The caller can compare this value
            // against ecc_bits to decide whether the nand erase block needs to
            // be recycled.
            uint32_t corrected_bit_flips;
        } rw;

        // NAND_OP_READ_PAGE_DATA_OOB, NAND_OP_WRITE_DATA_OOB, NAND_OP_READ_PAGE0
        struct {
            uint32_t command;            // Command.
            uint32_t page_num;           // NAND page to read/write
            struct {
                zx_handle_t vmo;         // vmo of data to read or write
                uint32_t length;         // data transfer length in pages
                                         // permitted values are 0 or 1
                                         // 0: no data to transfer
                                         // 1: transfer exactly 1 page
                uint64_t offset_vmo;     // vmo offset in pages
            } data;
            struct {
                zx_handle_t vmo;         // vmo of data to read or write.
                uint32_t length;         // transfer length in bytes.
                                         // 0: no oob to transfer
                                         // > 0: transfer oob bytes
                uint64_t offset_vmo;     // vmo offset in *bytes*.
            } oob;
            // Return value from READ_PAGE_DATA_OOB, max corrected bit flips in any
            // underlying ECC chunk read. The caller can compare this value
            // against ecc_bits to decide whether the nand erase block needs to
            // be recycled.
            uint32_t corrected_bit_flips;
        } rw_data_oob;

        // TODO: Remove this
        struct {
            // This operation reads or writes OOB data for a single page.
            uint32_t command;            // Command.
            zx_handle_t vmo;             // vmo of data to read or write.
            uint32_t length;             // Transfer length in bytes.
                                         // (0 is invalid).
            uint32_t page_num;           // Offset into nand, in pages.
            uint64_t offset_vmo;         // vmo offset in bytes.
            // Return value from READ_OOB, max corrected bit flips in any
            // underlying ECC chunk read in order to access the OOB data. The
            // caller can compare this value against ecc_bits to decide whether
            // the nand erase block needs to be recycled.
            uint32_t corrected_bit_flips;
        } oob;

        // NAND_OP_ERASE.
        struct {
            uint32_t command;            // Command.
            uint32_t first_block;        // Offset into nand, in erase blocks.
            uint32_t num_blocks;         // Number of blocks to erase.
                                         // (0 is invalid).
        } erase;
    };

    // The completion_cb() will be called when the nand operation succeeds or
    // fails.
    void (*completion_cb)(nand_op_t* op, zx_status_t status);

    // This is a caller-owned field that is not modified by the driver stack.
    void *cookie;
};

typedef struct nand_protocol_ops {
    // Obtains the parameters of the nand device (nand_info_t) and the required
    // size of nand_op_t. The nand_op_t's submitted via queue() must have
    // nand_op_size_out - sizeof(nand_op_t) bytes available at the end of the
    // structure for the use of the driver.
    void (*query)(void* ctx, nand_info_t* info_out, size_t* nand_op_size_out);

    // Submits an IO request for processing. Success or failure will be reported
    // via the completion_cb() in the nand_op_t. The callback may be called
    // before the queue() method returns.
    void (*queue)(void* ctx, nand_op_t* op);

    // Gets the list of bad erase blocks, as reported by the nand manufacturer.
    // The caller must allocate a table large enough to hold the expected number
    // of entries, and pass the size of that table on |bad_block_len|.
    // On return, |num_bad_blocks| contains the number of bad blocks found.
    // This should only be called before writing any data to the nand, and the
    // returned data should be saved somewhere else, along blocks that become
    // bad after they've been in use.
    void (*get_bad_block_list)(void* ctx, uint32_t* bad_blocks,
                               uint32_t bad_block_len,
                               uint32_t* num_bad_blocks);
} nand_protocol_ops_t;

typedef struct nand_protocol {
    nand_protocol_ops_t* ops;
    void* ctx;
} nand_protocol_t;
