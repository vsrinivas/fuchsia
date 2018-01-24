// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stdint.h>
#include <zircon/device/block.h>

// block_op_t's are submitted for processing via the queue() method
// of the block_protocol.  Once submitted, the contents of the block_op_t
// may be modified while it's being processed and/or as it is passed down
// the stack to lower layered drivers.
//
// The contents may be mutated along the way -- for example, a partition
// driver would, after validation, adjust offset_dev to reflect the position
// of the partition.
//
// The completion_cb() must eventually be called upon success or failure and
// at that point the cookie field must contain whatever value was in it when
// the block_op_t was originally queued.
//
// The rw.pages field may be modified but the *contents* of the array it points
// to may not be modified.

typedef struct block_op block_op_t;

struct block_op {
    union {
        // All Commands
        uint32_t command;                // command and flags

        // BLOCK_OP_READ, BLOCK_OP_WRITE
        struct {
            uint32_t command;            // command and flags
            uint32_t extra;              // available for temporary use
            zx_handle_t vmo;             // vmo of data to read or write
            uint32_t length;             // transfer length in blocks (0 is invalid)
            uint64_t offset_dev;         // device offset in blocks
            uint64_t offset_vmo;         // vmo offset in blocks
            uint64_t* pages;             // optional physical page list
        } rw;

        // BLOCK_OP_TRIM
        struct {
            uint32_t command;            // command and flags
            // ???
        } trim;
    };

    // The completion_cb() will be called when the block operation
    // succeeds or fails, and cookie will be whatever was set when
    // the block_op was initially queue()'d.
    void (*completion_cb)(block_op_t* block, zx_status_t status);
    void* cookie;
};

static_assert(sizeof(block_op_t) == 56, "");

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
#define BLOCK_OP_READ                0x00000001
#define BLOCK_OP_WRITE               0x00000002

// Write any controller or device cached data to nonvolatile storage.
// This operation always implies BARRIER_BEFORE and BARRIER_AFTER,
// meaning that previous operations will complete before it starts
// and later operations will not start until it is done.
#define BLOCK_OP_FLUSH               0x00000003

// TBD
#define BLOCK_OP_TRIM                0x00000004

#define BLOCK_OP_MASK                0x000000FF


// Mark this operation as "Force Unit Access" (FUA), indicating that
// it should not complete until the data is written to the non-volatile
// medium (write), and that reads should bypass any on-device caches.
#define BLOCK_FL_FORCE_ACCESS        0x00001000

// Require that this operation will not begin until all previous
// operations have completed.
//
// Prevents earlier operations from being reordered after this one.
#define BLOCK_FL_BARRIER_BEFORE      0x00000100

// Require that this operation complete before any subsequent
// operations are started.
//
// Prevents later operations from being reordered before this one.
#define BLOCK_FL_BARRIER_AFTER       0x00000200
