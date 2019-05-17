// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_IO_BUFFER_H_
#define DDK_IO_BUFFER_H_

#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

__BEGIN_CDECLS

// Sentinel value for io_buffer_t's |phys| field for when it is not valid.
#define IO_BUFFER_INVALID_PHYS 0

typedef struct {
    zx_handle_t bti_handle; // borrowed by library
    zx_handle_t vmo_handle; // owned by library
    zx_handle_t pmt_handle; // owned by library
    size_t size;
    zx_off_t offset;
    void* virt;
    // Points to the physical page backing the start of the VMO, if this
    // io buffer was created with the IO_BUFFER_CONTIG flag.
    zx_paddr_t phys;

    // This is used for storing the addresses of the physical pages backing non
    // contiguous buffers and is set by io_buffer_physmap().
    // Each entry in the list represents a whole page and the first entry
    // points to the page containing 'offset'.
    zx_paddr_t* phys_list;
    uint64_t phys_count;
} io_buffer_t;

enum {
    IO_BUFFER_RO         = (0 << 0),    // map buffer read-only
    IO_BUFFER_RW         = (1 << 0),    // map buffer read/write
    IO_BUFFER_CONTIG     = (1 << 1),    // allocate physically contiguous buffer
    IO_BUFFER_UNCACHED   = (1 << 2),    // map buffer with ZX_CACHE_POLICY_UNCACHED
    IO_BUFFER_FLAGS_MASK = IO_BUFFER_RW | IO_BUFFER_CONTIG | IO_BUFFER_UNCACHED,
};

// Initializes a new io_buffer.  If this call fails, it is still safe to call
// io_buffer_release on |buffer|.  |bti| is borrowed by the io_buffer and may be
// used throughout the lifetime of the io_buffer.
zx_status_t io_buffer_init(io_buffer_t* buffer, zx_handle_t bti, size_t size, uint32_t flags);
// An alignment of zero is interpreted as requesting page alignment.
// Requesting a specific alignment is not supported for non-contiguous buffers,
// pass zero for |alignment_log2| if not passing IO_BUFFER_CONTIG.  |bti| is borrowed
// by the io_buffer and may be used throughout the lifetime of the io_buffer.
zx_status_t io_buffer_init_aligned(io_buffer_t* buffer, zx_handle_t bti, size_t size,
                                   uint32_t alignment_log2, uint32_t flags);

// Initializes an io_buffer base on an existing VMO.
// duplicates the provided vmo_handle - does not take ownership
// |bti| is borrowed by the io_buffer and may be used throughout the lifetime of the io_buffer.
zx_status_t io_buffer_init_vmo(io_buffer_t* buffer, zx_handle_t bti, zx_handle_t vmo_handle,
                               zx_off_t offset, uint32_t flags);

zx_status_t io_buffer_cache_op(io_buffer_t* buffer, const uint32_t op,
                               const zx_off_t offset, const size_t size);

// io_buffer_cache_flush() performs a cache flush on a range of memory in the buffer
zx_status_t io_buffer_cache_flush(io_buffer_t* buffer, zx_off_t offset, size_t length);

// io_buffer_cache_flush_invalidate() performs a cache flush and invalidate on a range of memory
// in the buffer
zx_status_t io_buffer_cache_flush_invalidate(io_buffer_t* buffer, zx_off_t offset, size_t length);

// Looks up the physical pages backing this buffer's vm object.
// This is used for non contiguous buffers.
// The 'phys_list' and 'phys_count' fields are set if this function succeeds.
zx_status_t io_buffer_physmap(io_buffer_t* buffer);

// Pins and returns the physical addresses corresponding to the requested subrange
// of the buffer.  Invoking zx_pmt_unpin() on pmt releases the pin and makes the
// addresses invalid to use.
zx_status_t io_buffer_physmap_range(io_buffer_t* buffer, zx_off_t offset,
                                    size_t length, size_t phys_count,
                                    zx_paddr_t* physmap, zx_handle_t* pmt);

// Releases an io_buffer
void io_buffer_release(io_buffer_t* buffer);

static inline bool io_buffer_is_valid(const io_buffer_t* buffer) {
    return (buffer->vmo_handle != ZX_HANDLE_INVALID);
}

static inline void* io_buffer_virt(const io_buffer_t* buffer) {
    return (void*)(((uintptr_t)buffer->virt) + buffer->offset);
}

static inline zx_paddr_t io_buffer_phys(const io_buffer_t* buffer) {
    ZX_DEBUG_ASSERT(buffer->phys != IO_BUFFER_INVALID_PHYS);
    return buffer->phys + buffer->offset;
}

// Returns the buffer size available after the given offset, relative to the
// io_buffer vmo offset.
static inline size_t io_buffer_size(const io_buffer_t* buffer, size_t offset) {
    size_t remaining = buffer->size - buffer->offset - offset;
    // May overflow.
    if (remaining > buffer->size) {
        remaining = 0;
    }
    return remaining;
}

__END_CDECLS

#ifdef __cplusplus

namespace ddk {

class IoBuffer {
public:
    IoBuffer() {}
    IoBuffer(IoBuffer&& other) {
        io_buffer_ = other.io_buffer_;
        other.io_buffer_ = {};
    }
    IoBuffer& operator=(IoBuffer&& other) {
        io_buffer_ = other.io_buffer_;
        other.io_buffer_ = {};
        return *this;
    }
    ~IoBuffer() { io_buffer_release(&io_buffer_); }

    inline void release() { io_buffer_release(&io_buffer_); }

    inline zx_status_t Init(zx_handle_t bti, size_t size, uint32_t flags) {
        return io_buffer_init(&io_buffer_, bti, size, flags);
    }
    inline zx_status_t InitAligned(zx_handle_t bti, size_t size, uint32_t alignment_log2,
                                   uint32_t flags) {
        return io_buffer_init_aligned(&io_buffer_, bti, size, alignment_log2, flags);
    }
    inline zx_status_t InitVmo(zx_handle_t bti, zx_handle_t vmo_handle, zx_off_t offset,
                               uint32_t flags) {
        return io_buffer_init_vmo(&io_buffer_, bti, vmo_handle, offset, flags);
    }

    inline zx_status_t CacheOp(uint32_t op, zx_off_t offset, size_t size) {
        return io_buffer_cache_op(&io_buffer_, op, offset, size);
    }
    inline zx_status_t CacheFlush(zx_off_t offset, size_t length) {
        return  io_buffer_cache_flush(&io_buffer_, offset, length);
    }
    inline zx_status_t CacheFlushInvalidate(zx_off_t offset, size_t length) {
        return  io_buffer_cache_flush_invalidate(&io_buffer_, offset, length);
    }

    inline zx_status_t PhysMap() {
        return io_buffer_physmap(&io_buffer_);
    }
    inline zx_status_t PhysMapRange(zx_off_t offset, size_t length, size_t phys_count,
                                    zx_paddr_t* physmap, zx_handle_t* pmt) {
        return io_buffer_physmap_range(&io_buffer_, offset, length, phys_count, physmap, pmt);
    }

    inline bool is_valid() const {
        return io_buffer_is_valid(&io_buffer_);
    }
    inline void* virt() const {
        return io_buffer_virt(&io_buffer_);
    }
    inline zx_paddr_t phys() const {
        return io_buffer_phys(&io_buffer_);
    }

    inline const zx_paddr_t* phys_list() const {
        return io_buffer_.phys_list;
    }
    inline uint64_t phys_count() const {
        return io_buffer_.phys_count;
    }
    inline size_t size() const {
        return io_buffer_.size;
    }

private:
    io_buffer_t io_buffer_ = {};
};

} // namespace ddk

#endif // __cplusplus

#endif  // DDK_IO_BUFFER_H_
