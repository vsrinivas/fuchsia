// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/listnode.h>
#include <ddk/driver.h>

__BEGIN_CDECLS;

typedef struct iotxn iotxn_t;
typedef struct iotxn_ops iotxn_ops_t;

// An IO Transaction (iotxn) is an object that records all the state
// necessary to accomplish an io operation -- the general (len/off)
// and protocol specific (eg, usb endpoint and transfer type) parameters
// as well as the underlying data (which may be in-line, out-of-line,
// contained in a VMO, etc).
//
// Since the format of the payload data may vary, methods are provided
// to access the payload data or obtain its physical address, as well
// a method to call when the io has completed or failed.
//
// Terminology: iotxns are queued against a processor by a requestor

// new device protcol hook
// The driver *must* complete the txn (success or failure) by
// calling txn->ops->complete(txn);
//
// void (*queue)(mx_device_t* dev, iotxn_t* txn);

// opcodes
#define IOTXN_OP_READ      1
#define IOTXN_OP_WRITE     2

// cache maintenance ops
#define IOTXN_CACHE_INVALIDATE        MX_VMO_OP_CACHE_INVALIDATE
#define IOTXN_CACHE_CLEAN             MX_VMO_OP_CACHE_CLEAN
#define IOTXN_CACHE_CLEAN_INVALIDATE  MX_VMO_OP_CACHE_CLEAN_INVALIDATE
#define IOTXN_CACHE_SYNC              MX_VMO_OP_CACHE_SYNC

// flags
//
// This iotxn should not begin before any iotxns queued ahead
// of it have completed.
#define IOTXN_SYNC_BEFORE  1
//
// This iotxn should complete before any iotxns queued after it
// are started.
#define IOTXN_SYNC_AFTER   2


struct iotxn {
    // basic request data
    // (filled in by requestor, read by processor)
    uint32_t opcode;
    uint32_t flags;
    mx_off_t offset;      // byte offset (in file/device) to transfer to/from
    mx_off_t length;      // number of bytes to transfer
    uint32_t protocol;    // identifies the protocol-specific data

    // response data
    // (filled in by processor before ops->complete() is called)
    mx_status_t status;   // status of transaction
    mx_off_t actual;      // number of bytes actually transferred (on success)

    // methods to operate on the iotxn
    iotxn_ops_t* ops;

    // protocol specific extra data
    // (filled in by requestor, read by processor, type identified by 'protocol')
    uint64_t protocol_data[6];

    // list node and context
    // the current "owner" of the iotxn may use these however desired
    // (eg, the requestor may use node to hold the iotxn on a free list
    // and when it's queued the processor may use node to hold the iotxn
    // in a transaction queue)
    list_node_t node;
    void *context;

    // The complete_cb() callback is set by the requestor and is
    // invoked by the 'complete' ops method when it is called by
    // the processor upon completion of the io operation.
    void (*complete_cb)(iotxn_t* txn, void* cookie);
    // Set by requestor for passing data to complete_cb callback
    void* cookie;

    // structure to this point is 132 bytes

    // extra requestor data
    // amount of space here depends on extra_size passed when
    // allocating the iotxn
    uint8_t extra[0];
};

#define iotxn_to(txn, type) ((type*) (txn)->extra)
#define iotxn_pdata(txn, type) ((type*) (txn)->protocol_data)


// create a new iotxn with payload space of data_size
// and extra storage space of extra_size
mx_status_t iotxn_alloc(iotxn_t** out, uint32_t flags, size_t data_size, size_t extra_size);

// creates a new iotxn based on a provided VMO buffer, offset and size
// this duplicates the provided vmo_handle
mx_status_t iotxn_alloc_vmo(iotxn_t** out, mx_handle_t vmo_handle, size_t data_size,
                            mx_off_t data_offset, size_t extra_size);

// queue an iotxn against a device
void iotxn_queue(mx_device_t* dev, iotxn_t* txn);


struct iotxn_ops {
    // complete() must be called by the processor when the io operation has
    // completed or failed and the iotxn and any virtual or physical memory
    // obtained from it may not be touched again by the processor.
    //
    // If physmap() was used and created a temporary buffer, the contents
    // of that buffer are copied back into the iotxn's buffer (if this was
    // a read operation)
    //
    // The iotxn's complete_cb() will be called as the last action of
    // this method.
    void (*complete)(iotxn_t* txn, mx_status_t status, mx_off_t actual);


    // copyfrom() copies data from the iotxn's data buffer
    // Out of range operations are ignored.
    void (*copyfrom)(iotxn_t* txn, void* data, size_t length, size_t offset);

    // copyto() copies data into an iotxn's data buffer.
    // Out of range operations are ignored.
    void (*copyto)(iotxn_t* txn, const void* data, size_t length, size_t offset);

    // physmap() returns the physical start address of a buffer containing
    // the iotxn's buffer data (on WRITE ops) or a buffer that will be
    // copied back to the iotxn's buffer data (on READ ops).  This may
    // be the buffer itself, or a temporary, depending on conditions.
    void (*physmap)(iotxn_t* txn, mx_paddr_t* addr);

    // mmap() returns a void* pointing at the data in the iotxn's buffer.
    // This may have to do an expensive memory map operation or copy data
    // to a local buffer.  copyfrom(), copyto(), or physmap() are almost
    // always a better option.
    void (*mmap)(iotxn_t* txn, void** data);

    // cacheop() performs a cache maintenance op against the iotxn's internal
    // buffer.
    void (*cacheop)(iotxn_t* txn, uint32_t op, size_t offset, size_t length);

    // clone() creates a new iotxn which shares the underlying data
    // storage with this one, suitable for a driver to use to make a
    // request of a driver it is stacked on top of.
    mx_status_t (*clone)(iotxn_t* txn, iotxn_t** out, size_t extra_size);


    // free the iotxn -- should be called only by the entity that allocated it
    void (*release)(iotxn_t* txn);
};

__END_CDECLS;
