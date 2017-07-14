// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <limits.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/device/ioctl.h>
#include <magenta/types.h>

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
// freed.
#define IOCTL_BLOCK_FVM_SHRINK \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 13)
#define IOCTL_BLOCK_FVM_DESTROY \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 14)
#define IOCTL_BLOCK_FVM_QUERY \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 15)

// Block Core ioctls (specific to each block device):

#define BLOCK_FLAG_READONLY 0x00000001
#define BLOCK_FLAG_REMOVABLE 0x00000002

typedef struct {
    uint64_t block_count;       // The number of blocks in this block device
    uint32_t block_size;        // The size of a single block
    uint32_t max_transfer_size; // Max worst-case size in bytes per transfer, 0 is no maximum
    uint32_t flags;
    uint32_t reserved;
} block_info_t;

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

// ssize_t ioctl_block_get_fifos(int fd, mx_handle_t* fifo_out);
IOCTL_WRAPPER_OUT(ioctl_block_get_fifos, IOCTL_BLOCK_GET_FIFOS, mx_handle_t);

typedef uint16_t vmoid_t;

// ssize_t ioctl_block_attach_vmo(int fd, mx_handle_t* in, vmoid_t* out_vmoid);
IOCTL_WRAPPER_INOUT(ioctl_block_attach_vmo, IOCTL_BLOCK_ATTACH_VMO, mx_handle_t, vmoid_t);

#define MAX_TXN_MESSAGES 16
#define MAX_TXN_COUNT 256

typedef uint16_t txnid_t;

// ssize_t ioctl_block_alloc_txn(int fd, txnid_t* out_txnid);
IOCTL_WRAPPER_OUT(ioctl_block_alloc_txn, IOCTL_BLOCK_ALLOC_TXN, txnid_t);

// ssize_t ioctl_block_free_txn(int fd, const size_t* in_txnid);
IOCTL_WRAPPER_IN(ioctl_block_free_txn, IOCTL_BLOCK_FREE_TXN, txnid_t);

// ssize_t ioctl_block_fifo_close(int fd);
IOCTL_WRAPPER(ioctl_block_fifo_close, IOCTL_BLOCK_FIFO_CLOSE);

#define GUID_LEN 16
#define NAME_LEN 24

typedef struct {
    size_t slice_count;
    uint8_t type[GUID_LEN];
    uint8_t guid[GUID_LEN];
    char name[NAME_LEN];
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
    size_t slice_size;   // Size of a single slice, in bytes
    size_t vslice_count; // Number of addressable slices
} fvm_info_t;

// ssize_t ioctl_block_fvm_query(int fd, fvm_info_t* info);
IOCTL_WRAPPER_OUT(ioctl_block_fvm_query, IOCTL_BLOCK_FVM_QUERY, fvm_info_t);

// Multiple Block IO operations may be sent at once before a response is actually sent back.
// Block IO ops may be sent concurrently to different vmoids, and they also may be sent
// to different transactions at any point in time. Up to MAX_TXN_COUNT transactions may
// be allocated at any point in time.
//
// "Transactions" are allocated with the "alloc_txn" ioctl. Allocating a transaction allows
// MAX_TXN_MESSAGES to be buffered at once on a single txn before receiving a response.
// Once a txn has been allocated, it can be re-used many times. It is recommended that
// transactions are allocated on a "per-thread" basis, and only freed on thread teardown.
//
// The protocol to communicate with a single txn is as follows:
// 1) SEND [N - 1] messages with an allocated txnid for any value of 1 <= N < MAX_TXN_MESSAGES.
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
// Each transaction reads or writes up to 'length' bytes from the device, starting at
// 'dev_offset', into the VMO associated with 'vmoid', starting at 'vmo_offset'.
// If the transaction is out of range, for example if 'length' is too large or if
// 'dev_offset' is beyond the end of the device, MX_ERR_OUT_OF_RANGE is returned.

#define BLOCKIO_READ 0x0001      // Reads from the Block device into the VMO
#define BLOCKIO_WRITE 0x0002     // Writes to the Block device from the VMO
#define BLOCKIO_SYNC 0x0003      // Unimplemented
#define BLOCKIO_CLOSE_VMO 0x0004 // Detaches the VMO from the block device; closes the handle to it.
#define BLOCKIO_OP_MASK 0x00FF

#define BLOCKIO_TXN_END 0x0100 // Expects response after request (and all previous) have completed
#define BLOCKIO_FLAG_MASK 0xFF00

typedef struct {
    txnid_t txnid;
    vmoid_t vmoid;
    uint16_t opcode;
    uint16_t reserved0;
    uint64_t length;
    uint64_t vmo_offset;
    uint64_t dev_offset;
} block_fifo_request_t;

typedef struct {
    txnid_t txnid;
    uint16_t reserved0;
    mx_status_t status;
    uint32_t count; // The number of messages in the transaction completed by the block server.
    uint32_t reserved1;
    uint64_t reserved2;
    uint64_t reserved3;
} block_fifo_response_t;

static_assert(sizeof(block_fifo_request_t) == sizeof(block_fifo_response_t),
              "FIFO messages are the same size in both directions");

#define BLOCK_FIFO_ESIZE (sizeof(block_fifo_request_t))
#define BLOCK_FIFO_MAX_DEPTH (4096 / BLOCK_FIFO_ESIZE)
