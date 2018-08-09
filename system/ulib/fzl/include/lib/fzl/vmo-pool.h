// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/array.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/limits.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

namespace fzl {

// This class is not thread safe.
// VMO Pools are collections of VMOs that are used together
// and share similiar properties.  The VMO pool is intended to be used
// by a content producer, as all VMOs in the pool are automatically
// mapped to a VMAR.  The VMO Pool adds lifecyle management as well,
// by keeping track of which vmos are 'locked'.  Although this class
// does not maintain any vmo handles, mapping the vmos into vmars retains
// ownership.
//
// VMO Pools are intended to act as one backing for BufferCollections.
class VmoPool {

public:
    // Initializes a VmoPool with a set of vmos. You can pass a vector of vmos,
    // or a vmo pointer and the number of vmos it should grab.
    // If successful, returns ZX_OK.
    zx_status_t Init(const fbl::Vector<zx::vmo>& vmos);
    zx_status_t Init(const zx::vmo* vmos, size_t num_vmos);

    // Resets the buffer locks and the 'in process' indicator.
    void Reset();

    // Finds the next available buffer, and sets that buffer as currently in progress.
    // Returns ZX_OK if successful, and, if buffer_index != nullptr, stores the
    // buffer index into buffer_index.
    // Returns ZX_ERR_NOT_FOUND if no buffers were available or ZX_ERR_BAD_STATE
    // if a buffer is in the currently in progress state.
    zx_status_t GetNewBuffer(uint32_t* buffer_index = nullptr);

    // Sets the currently in progress buffer as completed and ready to consume.
    // The buffer will be locked for CPU reads until BufferRelease is called
    // with its index.  'Locked' in this context means that GetNewBuffer will
    // not set this buffer to the current buffer.
    // Returns ZX_OK if successful, or ZX_ERR_BAD_STATE if no buffer is
    // currently in progress.
    // If buffer_index != nullptr, the currently in progress buffer index
    // will be returned here.
    zx_status_t BufferCompleted(uint32_t* buffer_index = nullptr);

    // Unlocks the buffer with the specified index and sets it as ready to be
    // reused.  It is permissible to call BufferRelease instead of
    // BufferCompleted, effectively cancelling use of the current buffer.
    // Returns ZX_OK if successful, or ZX_ERR_NOT_FOUND if no locked buffer
    // was found with the given index. If the index is out of bounds,
    // ZX_ERROR_INVALID_ARGS will be returned.
    zx_status_t BufferRelease(uint32_t buffer_index);

    inline bool HasBufferInProgress() const {
        return current_buffer_ != kInvalidCurBuffer;
    }

    // Return the size of the current buffer.  Returns 0 if no current buffer.
    uint64_t CurrentBufferSize() const;

    // Return the start address of the current buffer.
    // Returns nullptr if no current buffer.
    void* CurrentBufferAddress() const;

    ~VmoPool();

private:
    struct ListableBuffer : public fbl::SinglyLinkedListable<ListableBuffer*> {
        VmoMapper buffer;
    };

    // The sentinel value for no in-progress buffer:
    static constexpr uint32_t kInvalidCurBuffer = fbl::numeric_limits<uint32_t>::max();
    // The buffer to which we are currently writing.
    uint32_t current_buffer_ = kInvalidCurBuffer;
    // VMO backing the buffer.
    fbl::Array<ListableBuffer> buffers_;
    // The list of free buffers.
    fbl::SinglyLinkedList<ListableBuffer*> free_buffers_;
};

} // namespace fzl
