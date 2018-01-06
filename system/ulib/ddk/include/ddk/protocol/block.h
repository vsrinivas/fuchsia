// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/block.h>

// block_op_t's are submitted for processing via the queue() method
// of the block_protocol.  Once submitted, the contents of the block_op_t
// may be modified while it's being processed and/or as it is passed down
// the stack to lower layered drivers.  The completion_cb() must eventually
// be called on success or failure and at that point the cookie field must
// contain whatever value was in it when the block_op_t was originally queued.
//
// The pages field may be modified but the *contents* of the array it points
// to may not be modified.

typedef struct block_op block_op_t;

struct block_op {
    union {
        uint16_t command;
        struct {
            uint16_t command;            // command and flags
            uint16_t length;             // transfer length in blocks - 1
            zx_handle_t vmo;             // vmo of data to read or write
            uint64_t offset_dev;         // device offset in blocks
            uint64_t offset_vmo;         // vmo offset in blocks
            uint64_t* pages;             // optional physical page list
        } rw;
        struct {
            uint16_t command;
            // ???
        } trim;
    } u;

    // Completion_cb() will be called when the block operation
    // succeeds or fails.
    void (*completion_cb)(block_op_t* block, zx_status_t status);
    void* cookie;                        // for use of original submitter
};

typedef struct block_protocol_ops {
    // Obtain the parameters of the block device (block_info_t) and
    // the required size of block_txn_t.  The block_txn_t's submitted
    // via queue() must have block_op_size_out - sizeof(block_op_t) bytes
    // available at the end of the structure for the use of the driver.
    void (*query)(void* ctx, block_info_t* info_out, size_t* block_op_size_out);

    // Submit an IO request for processing.  Success or failure will
    // be reported via the completion_cb() in the block_op_t.  This
    // callback may be called before the queue() method returns.
    void (*queue)(void* ctx, block_op_t* txn);
} block_protocol_ops_t;

typedef struct block_protocol {
    block_protocol_ops_t* ops;
    void* ctx;
} block_protocol_t;

// Read and Write ops use u.rw for parameters.
//
// If u.rw.pages is not NULL, the VMO is already appropriately pinned
// for IO and pages is an array of the physical addresses covering
// offset_vmo * block_size through (offset_vmo + length + 1U) * block_size.
//
// The number of entries in this array is always
// ((u.rw.length + 1U * block_size + PAGE_SIZE - 1) / PAGE_SIZE)
#define BLOCK_OP_READ                0x0001
#define BLOCK_OP_WRITE               0x0002

// Write any controller or device cached data to nonvolatile storage.
// This operation always implies BARRIER_BEFORE and BARRIER_AFTER,
// meaning that previous operations will complete before it starts
// and later operations will not start until it is done.
#define BLOCK_OP_FLUSH               0x0003

// TBD
#define BLOCK_OP_TRIM                0x0004

#define BLOCK_OP_MASK                0x000F


// Mark this operation as "Force Unit Access" (FUA), indicating that
// it should not complete until the data is written to the non-volatile
// medium (write), and that reads should bypass any on-device caches.
#define BLOCK_FL_FORCE_ACCESS        0x0100

// Require that this operation will not begin until all previous
// operations have completed.
//
// Prevents earlier operations from being reordered after this one.
#define BLOCK_FL_BARRIER_BEFORE      0x0010

// Require that this operation complete before any subsequent
// operations are started.
//
// Prevents later operations from being reordered before this one.
#define BLOCK_FL_BARRIER_AFTER       0x0020


// Maximum blocks allowed in one transfer
#define BLOCK_XFER_MAX_BLOCKS        65536
