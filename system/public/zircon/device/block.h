// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <limits.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/device/ioctl.h>
#include <zircon/types.h>

// Get information about the underlying block device.
#define IOCTL_BLOCK_GET_INFO \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 1)
// Get the type GUID of the partition (if one exists)
#define IOCTL_BLOCK_GET_TYPE_GUID \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 2)
// Get the GUID of the partition (if one exists)
#define IOCTL_BLOCK_GET_PARTITION_GUID \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 3)
// Get the name of the partition (if one exists)
#define IOCTL_BLOCK_GET_NAME \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 4)
// Rebind the block device (if supported)
#define IOCTL_BLOCK_RR_PART \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 5)
// Set up a FIFO-based server on the block device; acquire the handle to it
#define IOCTL_BLOCK_GET_FIFOS \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_BLOCK, 6)
// Attach a VMO to the currently running FIFO server
#define IOCTL_BLOCK_ATTACH_VMO \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_BLOCK, 7)
// Allocate a txn with the currently running FIFO server
#define IOCTL_BLOCK_ALLOC_TXN \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 8)
// Free a txn from the currently running FIFO server
#define IOCTL_BLOCK_FREE_TXN \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 9)
// Shut down the fifo server, waiting for it to be ready to be started again.
// Only necessary to guarantee availibility to the next fifo server client;
// otherwise, closing the client fifo is sufficient to shut down the server.
#define IOCTL_BLOCK_FIFO_CLOSE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 10)
// Allocate a virtual partition with the requested length
#define IOCTL_BLOCK_FVM_ALLOC \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 11)
// Extend a virtual partition.
#define IOCTL_BLOCK_FVM_EXTEND \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 12)
// Shink a virtual partition. Returns "success" if ANY slices are
// freed, even if part of the requested range contains unallocated slices.
#define IOCTL_BLOCK_FVM_SHRINK \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 13)
#define IOCTL_BLOCK_FVM_DESTROY \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 14)
// Returns the total number of vslices and slice size for an FVM partition
#define IOCTL_BLOCK_FVM_QUERY \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 15)
// Given a number of initial vslices, returns the number of contiguous allocated
// (or unallocated) vslices starting from each vslice.
#define IOCTL_BLOCK_FVM_VSLICE_QUERY \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 16)
// Atomically marks a vpartition (by instance GUID) as inactive, while finding
// another partition (by instance GUID) and marking it as active.
//
// If the "old" partition does not exist, the GUID is ignored.
// If the "old" partition is the same as the "new" partition, the "old"
// GUID is ignored (as in, "Upgrade" only activates).
// If the "new" partition does not exist, |ZX_ERR_NOT_FOUND| is returned.
//
// This function does not destroy the "old" partition, it just marks it as
// inactive -- to reclaim that space, the "old" partition must be explicitly
// destroyed.  This destruction can also occur automatically when the FVM driver
// is rebound (i.e., on reboot).
//
// This function may be useful for A/B updates within the FVM,
// since it will allow "activating" updated partitions.
#define IOCTL_BLOCK_FVM_UPGRADE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 17)
// Prints stats about the block device to the provided buffer and optionally
// clears the counters
#define IOCTL_BLOCK_GET_STATS   \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 18)

// Block Impl ioctls (specific to each block device):

#define BLOCK_FLAG_READONLY 0x00000001
#define BLOCK_FLAG_REMOVABLE 0x00000002

typedef struct {
    uint64_t block_count;       // The number of blocks in this block device
    uint32_t block_size;        // The size of a single block
    uint32_t max_transfer_size; // Max worst-case size in bytes per transfer, 0 is no maximum
    uint32_t flags;
    uint32_t reserved;
} block_info_t;

typedef struct {
    size_t max_concur;      // The maximum number of concurrent ops
    size_t max_pending;     // The maximum number of pending block ops
    size_t total_ops;       // Total number of block ops processed
    size_t total_blocks;    // Total number of blocks processed
} block_stats_t;

// ssize_t ioctl_block_get_info(int fd, block_info_t* out);
IOCTL_WRAPPER_OUT(ioctl_block_get_info, IOCTL_BLOCK_GET_INFO, block_info_t);

// ssize_t ioctl_block_get_type_guid(int fd, void* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_block_get_type_guid, IOCTL_BLOCK_GET_TYPE_GUID, void);

// ssize_t ioctl_block_get_partition_guid(int fd, void* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_block_get_partition_guid, IOCTL_BLOCK_GET_PARTITION_GUID, void);

// ssize_t ioctl_block_get_name(int fd, char* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_block_get_name, IOCTL_BLOCK_GET_NAME, char);

// ssize_t ioctl_block_rr_part(int fd);
IOCTL_WRAPPER(ioctl_block_rr_part, IOCTL_BLOCK_RR_PART);

// TODO(smklein): Move these to a separate file
// Block Device ioctls (shared between all block devices):

// ssize_t ioctl_block_get_fifos(int fd, zx_handle_t* fifo_out);
IOCTL_WRAPPER_OUT(ioctl_block_get_fifos, IOCTL_BLOCK_GET_FIFOS, zx_handle_t);

typedef uint16_t vmoid_t;

// Dummy vmoid value reserved for "invalid". Will never be allocated; can be
// used as a local value for unallocated / freed ID.
#define VMOID_INVALID 0

// ssize_t ioctl_block_attach_vmo(int fd, zx_handle_t* in, vmoid_t* out_vmoid);
IOCTL_WRAPPER_INOUT(ioctl_block_attach_vmo, IOCTL_BLOCK_ATTACH_VMO, zx_handle_t, vmoid_t);

#define MAX_TXN_COUNT 256

typedef uint16_t txnid_t;

// Dummy TXNID value reserved for "invalid". Will never be allocated; can be
// used as a local value for unallocated / freed ID.
#define TXNID_INVALID 0xFFFF

static_assert(TXNID_INVALID > MAX_TXN_COUNT, "Invalid Txn ID may be valid");

// ssize_t ioctl_block_alloc_txn(int fd, txnid_t* out_txnid);
IOCTL_WRAPPER_OUT(ioctl_block_alloc_txn, IOCTL_BLOCK_ALLOC_TXN, txnid_t);

// ssize_t ioctl_block_free_txn(int fd, const size_t* in_txnid);
IOCTL_WRAPPER_IN(ioctl_block_free_txn, IOCTL_BLOCK_FREE_TXN, txnid_t);

// ssize_t ioctl_block_fifo_close(int fd);
IOCTL_WRAPPER(ioctl_block_fifo_close, IOCTL_BLOCK_FIFO_CLOSE);

#define GUID_LEN 16
#define NAME_LEN 24
#define MAX_FVM_VSLICE_REQUESTS 16

typedef struct {
    size_t slice_count;
    uint8_t type[GUID_LEN];
    uint8_t guid[GUID_LEN];
    char name[NAME_LEN];
    uint32_t flags; // Refer to fvm.h for options here; default is zero.
} alloc_req_t;

// ssize_t ioctl_block_fvm_alloc(int fd, const alloc_req_t* req);
IOCTL_WRAPPER_IN(ioctl_block_fvm_alloc, IOCTL_BLOCK_FVM_ALLOC, alloc_req_t);

typedef struct {
    size_t offset; // Both in units of "slice". "0" = slice 0, "1" = slice 1, etc...
    size_t length;
} extend_request_t;

// ssize_t ioctl_block_fvm_extend(int fd, const extend_request_t* request);
IOCTL_WRAPPER_IN(ioctl_block_fvm_extend, IOCTL_BLOCK_FVM_EXTEND, extend_request_t);

// ssize_t ioctl_block_fvm_shrink(int fd, const extend_request_t* request);
IOCTL_WRAPPER_IN(ioctl_block_fvm_shrink, IOCTL_BLOCK_FVM_SHRINK, extend_request_t);

// ssize_t ioctl_block_fvm_destroy(int fd);
IOCTL_WRAPPER(ioctl_block_fvm_destroy, IOCTL_BLOCK_FVM_DESTROY);

typedef struct {
    bool allocated; // true if vslices are allocated, false otherwise
    size_t count; // number of contiguous vslices
} vslice_range_t;

typedef struct {
    size_t count; // number of elements in vslice_start
    size_t vslice_start[MAX_FVM_VSLICE_REQUESTS]; // vslices to query from
} query_request_t;

typedef struct {
    size_t count; // number of elements in vslice_range
    vslice_range_t vslice_range[MAX_FVM_VSLICE_REQUESTS]; // number of contiguous vslices
                                                          // that are allocated (or unallocated)
} query_response_t;

typedef struct {
    size_t slice_size;   // Size of a single slice, in bytes
    size_t vslice_count; // Number of addressable slices
} fvm_info_t;

// ssize_t ioctl_block_fvm_query(int fd, fvm_info_t* info);
IOCTL_WRAPPER_OUT(ioctl_block_fvm_query, IOCTL_BLOCK_FVM_QUERY, fvm_info_t);

// ssize_t ioctl_block_fvm_vslice_query(int fd, query_request_t* request,
//                                      query_response_t* response);
IOCTL_WRAPPER_INOUT(ioctl_block_fvm_vslice_query, IOCTL_BLOCK_FVM_VSLICE_QUERY,
                    query_request_t, query_response_t);

typedef struct {
    uint8_t old_guid[GUID_LEN];
    uint8_t new_guid[GUID_LEN];
} upgrade_req_t;

// ssize_t ioctl_block_fvm_upgrade(int fd, const upgrade_req_t* req);
IOCTL_WRAPPER_IN(ioctl_block_fvm_upgrade, IOCTL_BLOCK_FVM_UPGRADE, upgrade_req_t);

// ssize_t ioctl_block_get_stats(int fd, bool clear, block_stats_t* out)
IOCTL_WRAPPER_INOUT(ioctl_block_get_stats, IOCTL_BLOCK_GET_STATS, bool, block_stats_t);

// Multiple Block IO operations may be sent at once before a response is actually sent back.
// Block IO ops may be sent concurrently to different vmoids, and they also may be sent
// to different transactions at any point in time. Up to MAX_TXN_COUNT transactions may
// be allocated at any point in time.
//
// "Transactions" are allocated with the "alloc_txn" ioctl. Allocating a transaction allows
// multiple message to be buffered at once on a single txn before receiving a response.
// Once a txn has been allocated, it can be re-used many times. It is recommended that
// transactions are allocated on a "per-thread" basis, and only freed on thread teardown.
//
// The protocol to communicate with a single txn is as follows:
// 1) SEND [N - 1] messages with an allocated txnid for any value of 1 <= N.
//    The BLOCKIO_TXN_END flag is not set for this step.
// 2) SEND a final Nth message with the same txnid, but also the BLOCKIO_TXN_END flag.
// 3) RECEIVE a single response from the Block IO server after all N requests have completed.
//    This response is sent once all operations either complete or a single operation fails.
//    At this point, step (1) may begin again without reallocating the txn.
//
// For BLOCKIO_READ and BLOCKIO_WRITE, N may be greater than 1.
// Otherwise, N == 1 (skipping step (1) in the protocol above).
//
// Notes:
// - txnids may operate on any number of vmoids at once.
// - If additional requests are sent on the same txnid before step (3) has completed, then
//   the additional request will not be processed. If BLOCKIO_TXN_END is set, an error will
//   be returned. Otherwise, the request will be silently dropped.
// - The only requests that receive responses are ones which have the BLOCKIO_TXN_END flag
//   set. This is the case for both successful and erroneous requests. This property allows
//   the Block IO server to send back a response on the FIFO without waiting.
//
// For example, the following is a valid sequence of transactions:
//   -> (txnid = 1, vmoid = 1, OP = Write)
//   -> (txnid = 1, vmoid = 2, OP = Write)
//   -> (txnid = 2, vmoid = 3, OP = Write | Want Reply)
//   <- Response sent to txnid = 2
//   -> (txnid = 1, vmoid = 1, OP = Read | Want Reply)
//   <- Response sent to txnid = 1
//   -> (txnid = 3, vmoid = 1, OP = Write)
//   -> (txnid = 3, vmoid = 1, OP = Read | Want Reply)
//   <- Repsonse sent to txnid = 3
//
// Each transaction reads or writes up to 'length' blocks from the device, starting at 'dev_offset'
// blocks, into the VMO associated with 'vmoid', starting at 'vmo_offset' blocks.  If the
// transaction is out of range, for example if 'length' is too large or if 'dev_offset' is beyond
// the end of the device, ZX_ERR_OUT_OF_RANGE is returned.

// Reads from the Block device into the VMO
#define BLOCKIO_READ           0x00000001
// Writes to the Block device from the VMO
#define BLOCKIO_WRITE          0x00000002
// Write any cached data to nonvolatile storage.
// Implies BARRIER_BEFORE and BARRIER_AFTER.
#define BLOCKIO_FLUSH          0x00000003
// Detaches the VMO from the block device.
#define BLOCKIO_CLOSE_VMO      0x00000004
#define BLOCKIO_OP_MASK        0x000000FF

// Require that this operation will not begin until all prior operations
// have completed.
#define BLOCKIO_BARRIER_BEFORE 0x00000100
// Require that this operation must complete before additional operations begin.
#define BLOCKIO_BARRIER_AFTER  0x00000200
// Respond after request (and all previous) have completed
#define BLOCKIO_TXN_END        0x00000400
#define BLOCKIO_FLAG_MASK      0x0000FF00

typedef struct {
    txnid_t txnid;
    vmoid_t vmoid;
    uint32_t opcode;
    uint64_t length;
    uint64_t vmo_offset;
    uint64_t dev_offset;
} block_fifo_request_t;

typedef struct {
    txnid_t txnid;
    uint16_t reserved0;
    zx_status_t status;
    uint32_t count; // The number of messages in the transaction completed by the block server.
    uint32_t reserved1;
    uint64_t reserved2;
    uint64_t reserved3;
} block_fifo_response_t;

static_assert(sizeof(block_fifo_request_t) == sizeof(block_fifo_response_t),
              "FIFO messages are the same size in both directions");

#define BLOCK_FIFO_ESIZE (sizeof(block_fifo_request_t))
#define BLOCK_FIFO_MAX_DEPTH (4096 / BLOCK_FIFO_ESIZE)
