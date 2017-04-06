// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/listnode.h>
#include <ddk/driver.h>
#include <sys/types.h>

__BEGIN_CDECLS;

typedef struct iotxn iotxn_t;

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
// calling iotxn_complete(txn);
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

typedef uint64_t iotxn_proto_data_t[6];
typedef uint64_t iotxn_extra_data_t[6];

typedef struct iotxn_sg {
    mx_paddr_t paddr;
    uint64_t length;
} iotxn_sg_t;

struct iotxn {
    // basic request data
    // (filled in by requestor, read by processor)
    uint32_t opcode;
    uint32_t flags;
    mx_off_t offset;      // byte offset (in file/device) to transfer to/from
    mx_off_t length;      // number of bytes to transfer
    uint32_t protocol;    // identifies the protocol-specific data

    // response data
    // (filled in by processor before iotxn_complete() is called)
    mx_status_t status;   // status of transaction
    mx_off_t actual;      // number of bytes actually transferred (on success)

    uint32_t pflags;      // private flags, do not set

    // data payload
    mx_handle_t vmo_handle;
    uint64_t vmo_offset;  // offset into the vmo to use for the buffer
    uint64_t vmo_length;  // buffer size starting at offset

    // TODO remove this after removing mmap()
    void* virt;           // mapped address of vmo

    // optional scatter list
    // the current "owner" of the iotxn may set these to specify physical
    // ranges backing the data payload. this field is also set by
    // iotxn_physmap() and iotxn_physmap_sg()
    iotxn_sg_t* sg;
    uint64_t sg_length;  // number of entries in scatter list

    // protocol specific extra data
    // (filled in by requestor, read by processor, type identified by 'protocol')
    iotxn_proto_data_t protocol_data;

    // extra requestor data
    iotxn_extra_data_t extra;

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

    // The release_cb() callback is set by the allocator and is
    // invoked by the 'iotxn_release' method when it is called
    // by the requestor.
    void (*release_cb)(iotxn_t* txn);
};

#define iotxn_pdata(txn, type) ((type*) (txn)->protocol_data)

// convenience function to convert an array of physical page addresses to
// iotxn_sg_t. 'len' is the number of entries in 'pages' and the 'sg' buffer
// must be at least 'len' entries long.
void iotxn_pages_to_sg(mx_paddr_t* pages, iotxn_sg_t* sg, uint32_t len, uint32_t* sg_len);

// flags for iotxn_alloc
#define IOTXN_ALLOC_CONTIGUOUS (1 << 0)    // allocate a contiguous vmo
#define IOTXN_ALLOC_POOL       (1 << 1)    // freelist this iotxn on iotxn_release

// create a new iotxn with payload space of data_size
mx_status_t iotxn_alloc(iotxn_t** out, uint32_t alloc_flags, uint64_t data_size);

// initializes a statically allocated iotxn based on provided VMO.
// calling iotxn_release() on this iotxn will free any resources allocated by the iotxn
// but not free the iotxn itself.
void iotxn_init(iotxn_t* txn, mx_handle_t vmo_handle, uint64_t vmo_offset, uint64_t length);

// queue an iotxn against a device
void iotxn_queue(mx_device_t* dev, iotxn_t* txn);

// iotxn_complete() must be called by the processor when the io operation has
// completed or failed and the iotxn and any virtual or physical memory obtained
// from it may not be touched again by the processor.
//
// The iotxn's complete_cb() will be called as the last action of
// this method.
void iotxn_complete(iotxn_t* txn, mx_status_t status, mx_off_t actual);

// iotxn_copyfrom() copies data from the iotxn's vm object.
// Out of range operations are ignored.
ssize_t iotxn_copyfrom(iotxn_t* txn, void* data, size_t length, size_t offset);

// iotxn_copyto() copies data into an iotxn's vm object.
// Out of range operations are ignored.
ssize_t iotxn_copyto(iotxn_t* txn, const void* data, size_t length, size_t offset);

// iotxn_physmap_sg() returns a list of physical ranges of the memory backing
// an iotxn's vm object.
mx_status_t iotxn_physmap(iotxn_t* txn, iotxn_sg_t** sg_out, uint32_t* sg_len);

// iotxn_mmap() maps the iotxn's vm object and returns the virtual address.
// iotxn_copyfrom(), iotxn_copyto(), or iotxn_ physmap() are almost always a
// better option.
mx_status_t iotxn_mmap(iotxn_t* txn, void** data);

// iotxn_cacheop() performs a cache maintenance op against the iotxn's internal
// buffer.
void iotxn_cacheop(iotxn_t* txn, uint32_t op, size_t offset, size_t length);

// iotxn_clone() creates a new iotxn which shares the vm object with this one,
// suitable for a driver to use to make a request of a driver it is stacked on
// top of.
mx_status_t iotxn_clone(iotxn_t* txn, iotxn_t** out);

// free the iotxn -- should be called only by the entity that allocated it
void iotxn_release(iotxn_t* txn);

__END_CDECLS;
