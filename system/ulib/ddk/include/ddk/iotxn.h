// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/listnode.h>
#include <ddk/driver.h>
#include <sys/types.h>
#include <limits.h>

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
                          // it is invalid to modify this value after initialization
    uint64_t vmo_length;  // buffer size starting at vmo_offset

    /* --- cacheline 1 boundary (64 bytes) --- */

    // optional physical pages list
    // the current "owner" of the iotxn may set these to specify physical
    // pages backing the data payload. this field is also set by
    // iotxn_physmap()
    // each entry in the list represents a whole page and the first entry
    // points to the page containing 'vmo_offset'.
    // NOTE: if phys_count == 1, the buffer is physically contiguous.
    mx_paddr_t* phys;
    uint64_t phys_count;  // number of entries in phys list

    // protocol specific extra data
    // (filled in by requestor, read by processor, type identified by 'protocol')
    // this field may be modified by any intermediate processors.
    iotxn_proto_data_t protocol_data;

    /* --- cacheline 2 boundary (128 bytes) --- */

    // extra requestor data
    // this field may not be modified by anyone except the requestor
    iotxn_extra_data_t extra;

    // list node and context
    // the current "owner" of the iotxn may use these however desired
    // (eg, the requestor may use node to hold the iotxn on a free list
    // and when it's queued the processor may use node to hold the iotxn
    // in a transaction queue)
    list_node_t node;

    /* --- cacheline 3 boundary (192 bytes) --- */

    void *context;

    // optional virtual address pointing to vmo_offset
    // the current "owner" of the iotxn may set this to specify a virtual
    // mapping of the vmo. this field is also set by iotxn_mmap()
    void* virt;           // mapped address of vmo

    // The complete_cb() callback is set by the requestor and is
    // invoked by the 'complete' ops method when it is called by
    // the processor upon completion of the io operation.
    void (*complete_cb)(iotxn_t* txn, void* cookie);
    // Set by requestor for passing data to complete_cb callback
    // May not be modified by anyone other than the requestor.
    void* cookie;

    // The release_cb() callback is set by the allocator and is
    // invoked by the 'iotxn_release' method when it is called
    // by the requestor.
    void (*release_cb)(iotxn_t* txn);

    // May be used by iotxn_physmap() to store the physical pages list
    // instead of allocating additional memory.
    mx_paddr_t phys_inline[3];
};

static_assert(offsetof(iotxn_t, phys) == 64, "phys should be at 64");
static_assert(offsetof(iotxn_t, extra) == 128, "extra should be at 128");
static_assert(offsetof(iotxn_t, context) == 192, "context should be at 192");

// used to iterate over contiguous buffer ranges in the physical address space
typedef struct {
    iotxn_t*    txn;        // txn we are operating on
    mx_off_t    offset;     // current offset in the txn (relative to iotxn vmo_offset)
    size_t      max_length; // max length to be returned by iotxn_phys_iter_next()
    uint64_t    page;       // index of page in txn->phys that contains offset
    uint64_t    last_page;  // last valid page index in txn->phys
} iotxn_phys_iter_t;

#define iotxn_pdata(txn, type) ((type*) (txn)->protocol_data)

// flags for iotxn_alloc
#define IOTXN_ALLOC_CONTIGUOUS (1 << 0)    // allocate a contiguous vmo
#define IOTXN_ALLOC_POOL       (1 << 1)    // freelist this iotxn on iotxn_release

// create a new iotxn with payload space of data_size
mx_status_t iotxn_alloc(iotxn_t** out, uint32_t alloc_flags, uint64_t data_size);

// create a new iotxn based on provided VMO.
mx_status_t iotxn_alloc_vmo(iotxn_t** out, uint32_t alloc_flags, mx_handle_t vmo_handle,
                            uint64_t vmo_offset, uint64_t length);

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

// iotxn_physmap() looks up the physical pages backing this iotxn's vm object.
// the 'phys' and 'phys_count' fields are set if this function succeeds.
mx_status_t iotxn_physmap(iotxn_t* txn);

// convenience function to get the physical address of iotxn, taking into
// account 'vmo_offset', For contiguous buffers this will return the physical
// address of the buffer. For noncontiguous buffers this will return the
// physical address of the first page.
static inline mx_paddr_t iotxn_phys(iotxn_t* txn) {
    if (!txn->phys) {
        return 0;
    }
    uint64_t unaligned = (txn->vmo_offset & (PAGE_SIZE - 1));
    return txn->phys[0] + unaligned;
}

// iotxn_mmap() maps the iotxn's vm object and returns the virtual address.
// iotxn_copyfrom(), iotxn_copyto(), or iotxn_ physmap() are almost always a
// better option.
mx_status_t iotxn_mmap(iotxn_t* txn, void** data);

// iotxn_cacheop() performs a cache maintenance op against the iotxn's internal
// buffer.
void iotxn_cacheop(iotxn_t* txn, uint32_t op, size_t offset, size_t length);

// iotxn_clone() creates a new iotxn which shares the vm object with this one,
// suitable for a driver to use to make a request of a driver it is stacked on
// top of. initializes the iotxn pointed to by 'out' if it is not null, and
// allocates a new one otherwise.
mx_status_t iotxn_clone(iotxn_t* txn, iotxn_t** out);

// iotxn_clone_partial() is similar to iotxn_clone(), but the cloned
// iotxn will have an updated vmo_offset and length. The new vmo_offset
// must be greater than or equal to the original's vmo_offset. The new
// length must be less than or equal to the original's length.
mx_status_t iotxn_clone_partial(iotxn_t* txn, uint64_t vmo_offset, mx_off_t length, iotxn_t** out);

// free the iotxn -- should be called only by the entity that allocated it
void iotxn_release(iotxn_t* txn);

// initializes an iotxn_phys_iter_t for an iotxn
// max_length is the maximum length of a range returned by iotxn_phys_iter_next()
// max_length must be either a positive multiple of PAGE_SIZE, or zero for no limit.
void iotxn_phys_iter_init(iotxn_phys_iter_t* iter, iotxn_t* txn, size_t max_length);

// returns the next physical address and length for the iterator up to size max_length.
// return value is length, or zero if iteration is done.
size_t iotxn_phys_iter_next(iotxn_phys_iter_t* iter, mx_paddr_t* out_paddr);

__END_CDECLS;
