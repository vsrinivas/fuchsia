// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fidl/coding.h>
#include <zircon/assert.h>

// TODO(dustingreen): Switch to llcpp, DecodedMessage, etc, probably.
template <typename FidlCStruct, auto FidlCMetaTable>
class FidlStruct {
public:
    // These are used to select which constructor.
    enum DefaultType { Default };
    enum NullType { Null };

    // For request structs, the request handler is expected to close all the
    // handles, but the incoming struct itself isn't owned by the hander, and
    // the incoming struct is const, which conflicts with managing handles by
    // zeroing a handle field when the handle is closed.  So for now, we always
    // copy the incoming struct, own the copy, and close the handles via the
    // copy.  The _dispatch() caller won't try to close the handles in its copy.

    FidlStruct(const FidlCStruct& to_copy_and_own_handles)
        // struct copy
        : storage_(to_copy_and_own_handles),
          ptr_(&storage_) {
        // nothing else to do here
    }

    // There is intentionally not a zero-arg constructor, to force selection
    // between starting with default-initialized storage with handles owned by
    // ptr_ (any handles set to non-zero value after construction), vs. starting
    // with ptr_ set to nullptr so a later reset() is faster.

    // For reply structs, it's useful to start with a default-initialized struct
    // that can get incrementally populated, with a partially-initialized struct
    // along the way that's still possible to clean up so handles get closed
    // properly even if the reply never gets fully built and/or never gets sent.
    FidlStruct(DefaultType not_used)
        : ptr_(&storage_) {
        // nothing else to do here
    }

    FidlStruct(NullType not_used)
        : ptr_(nullptr) {
        // nothing else to do here
    }

    // Close any handles that aren't currently ZX_HANDLE_INVALID.  The client
    // code can choose to move a handle out to be owned separately by setting
    // the handle field to ZX_HANDLE_INVALID (or leaving it 0 which is the same
    // thing).
    ~FidlStruct() {
        reset_internal(nullptr);
    }

    void reset(const FidlCStruct* to_copy_and_own_handles) {
        ZX_DEBUG_ASSERT_COND(!is_moved_out_);
        reset_internal(to_copy_and_own_handles);
    }

    // Stop managing the handles, and return a pointer for the caller's
    // convenience.  After this, get() will return nullptr to discourage further
    // use of non-owned handle fields.
    //
    // The caller must stop using the returned value before the earlier of when
    // this instance is deleted or when this instance is re-used.
    FidlCStruct* release() {
        ZX_DEBUG_ASSERT_COND(!is_moved_out_);
        ZX_DEBUG_ASSERT(ptr_);
        FidlCStruct* tmp = ptr_;
        ptr_ = nullptr;
        return tmp;
    }

    // Return value can be nullptr if release() has been called previously.
    FidlCStruct* get() {
        ZX_DEBUG_ASSERT_COND(!is_moved_out_);
        return ptr_;
    }

    bool is_valid() {
        ZX_DEBUG_ASSERT_COND(!is_moved_out_);
        return !!ptr_;
    }

    operator bool() {
        ZX_DEBUG_ASSERT_COND(!is_moved_out_);
        return !!ptr_;
    }

    FidlCStruct* operator->() {
        ZX_DEBUG_ASSERT_COND(!is_moved_out_);
        ZX_DEBUG_ASSERT(ptr_);
        return ptr_;
    }

    FidlCStruct& operator*() {
        ZX_DEBUG_ASSERT_COND(!is_moved_out_);
        ZX_DEBUG_ASSERT(ptr_);
        return *ptr_;
    }

    // transfer handle ownership, copy the data, invalidate the source
    FidlStruct(FidlStruct&& to_move) {
        reset(to_move.release_allow_null());
#if ZX_DEBUG_ASSERT_IMPLEMENTED
        to_move.is_moved_out_ = true;
#endif
    }

    // transfer handle ownership, copy the data, invalidate the source
    FidlStruct& operator=(FidlStruct&& to_move) {
        reset(to_move.release_allow_null());
#if ZX_DEBUG_ASSERT_IMPLEMENTED
        to_move.is_moved_out_ = true;
#endif
        return *this;
    }

private:
    void reset_internal(const FidlCStruct* to_copy_and_own_handles) {
        if (ptr_) {
            fidl_close_handles(FidlCMetaTable, ptr_, nullptr);
        }
        if (to_copy_and_own_handles) {
            storage_ = *to_copy_and_own_handles;
            ptr_ = &storage_;
        } else {
            ptr_ = nullptr;
        }
    }

    // Same as release, but don't assert on ptr_. This allows moving from a null
    // struct.
    FidlCStruct* release_allow_null() {
        ZX_DEBUG_ASSERT_COND(!is_moved_out_);
        FidlCStruct* tmp = ptr_;
        ptr_ = nullptr;
        return tmp;
    }

    FidlCStruct storage_{};
    FidlCStruct* ptr_ = nullptr;

#if ZX_DEBUG_ASSERT_IMPLEMENTED
    bool is_moved_out_ = false;
#endif

    FidlStruct(FidlStruct& to_copy) = delete;
    FidlStruct& operator=(FidlStruct& to_copy) = delete;
};
