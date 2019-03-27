// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <bitmap/bitmap.h>
#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>

#include <limits.h>
#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/type_support.h>
#include <zircon/assert.h>
#include <zircon/types.h>

namespace id_allocator {
#ifdef __Fuchsia__
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::VmoStorage>;
#else
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::DefaultStorage>;
#endif

// IdAllocator treats ids like a resource; one can reserve and free - one
// at a time.
// Internally to keep allocation and free light-weight (O(logn)), we use
// a tree like structure. Specifically, it is a 64-nary tree.
// Each node is a bit. Leaf bit represents state, reserved or not, of the id.
// Non-leaf, parent bit is set only if all the child bits are set.
//
// During allocation, we walk down a path only if a non-leaf bit is unset.
// On finding an unset leaf bit, we set the bit and recursively set it's
// parent bit only if all children bits are set.
//
// Freeing an id involves clearing the leaf bit and clearing parent bit only
// when it is set.
//
// 64-nary tree is chosen because architecture/compilers support faster first
// set/unset bit lookup (through likes of __builtin_ffs)
//
// This class is thread-compatible
//
class IdAllocator {

public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(IdAllocator);

    // Creates a new allocator to manage allocation and free of id_count number
    // of ids. On success, returns ZX_OK. This function may fail for more than
    // one reason including but not limited to too large id_count
    // (ZX_ERR_OUT_OF_RANGE).
    static zx_status_t Create(size_t id_count,
                              std::unique_ptr<IdAllocator>* ida_out);

    // Find and allocate an id that is not busy. Returns ZX_OK on success and
    // allocated id in in *out_id*
    zx_status_t Allocate(size_t* out_id);

    // Marks given *id* as busy. Returns ZX_OK if the id was free
    zx_status_t MarkAllocated(size_t id);

    // Frees an allocated id. Returns non-ZX_OK value on error
    zx_status_t Free(size_t id);

    // Returns true if the given id is busy. All ids>=id_count_ are considered
    // free. Allows client to test state of a given id.
    bool IsBusy(size_t id) const;

    // Grows allocator in terms of how many ids it could allocate. non-ZX_OK
    // value is returned on failure.
    zx_status_t Grow(size_t size);

    // Shrinks number of ids the existing IdAllocator can allocate. It may
    // free memory associated with the allocator and may fail.
    zx_status_t Shrink(size_t id_count);

    // Reset marks all ids free in the allocator. This may change the
    // size if id_count is not same as current id_count_ and may fail
    // during allocation/free.
    zx_status_t Reset(size_t id_count);

    // Returns current count of number of ids being managed.
    size_t Size() const { return id_count_; }

    // Human readable dump of the tree.
    void Dump() const;

private:
    IdAllocator()
        : id_count_(0), level_count_(0){}

    // Returns number of bits required in a "level" for id_count ids
    size_t LevelBitCount(int8_t level) const;

    // Returns size_t rounded number of bits for a level
    size_t LevelBitCountRounded(int8_t level) const;

    // Set a bit at *bit* in *level*. Returns true if all sister bits
    // of the *bit* are set.
    bool SetBitAt(int8_t level, size_t bit);

    // Clear a bit at *bit* in *level*.
    void ClearBitAt(int8_t level, size_t bit);

    // Returns first unset bit in *level* between index base_index and
    // (base_index + kMaxChildren). Expects kMaxChildren aligned base_index.
    // Returns kMaxId if there is no unset bit in that range.
    size_t FindFirstUnset(int8_t level, size_t base_index) const;

    // Find an id which is not busy. If all ids are busy then returns
    // kIdMax
    size_t Find() const;

    // Mark a bit busy at given level.
    // Adjusts parents' state, if necessary
    void MarkBusyInternal(size_t id, int8_t level);
    // Mark a given id busy
    void MarkBusy(size_t id);

    // Mark a bit free at given level.
    // Adjusts parents' state, if necessary
    void MarkFreeInternal(size_t id, int8_t level);
    // Mark a given id free
    void MarkFree(size_t id);

    // Marks all the bits busy that are not used in addressing id_count_ bits
    // in given a level.
    void MarkUnallocatable(int8_t level);

    // Marks all the bits free that are not used in addressing id_count_ bits
    // in given a level. During Grow, this routine is used to undo all the effect
    // of MarkUnallocatable called during previous Grow/Reset
    void MarkAllAllocatable(int8_t level);

    // Grows each RawBitmap. Returns ZX_OK on success.
    zx_status_t GrowInternal(size_t id_count);

    // This is 64-nary tree. 11 levels give 2^66 addressability which is
    // sufficient on 64-bit arch.
    static const int8_t kMaxLevels = 11;

    // levels_[0] is leaf bit and levels_[n > 0] are intermediate nodes.
    // This keeps Grow simple and lets us tree grow upwards.
    RawBitmap levels_[kMaxLevels];
    size_t id_count_;
    int8_t level_count_;
};

} // namespace id_allocator
