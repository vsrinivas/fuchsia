// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>

#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>

#include <lib/fidl/internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

#include "buffer_walker.h"

#include <stdio.h>

// TODO(kulakowski) Design zx_status_t error values.

namespace {

class FidlEncoder final : public fidl::internal::BufferWalker<FidlEncoder, true, true> {
    typedef fidl::internal::BufferWalker<FidlEncoder, true, true> Super;

public:
    FidlEncoder(const fidl_type_t* type, void* bytes, uint32_t num_bytes, zx_handle_t* handles,
                uint32_t num_handles, uint32_t* out_actual_handles, const char** out_error_msg)
        : Super(type), bytes_(static_cast<uint8_t*>(bytes)), num_bytes_(num_bytes),
          handles_(handles), num_handles_(num_handles), out_actual_handles_(out_actual_handles),
          out_error_msg_(out_error_msg) {}

    void Walk() {
        if (handles_ == nullptr && num_handles_ != 0u) {
            SetError("Cannot provide non-zero handle count and null handle pointer");
            return;
        }
        if (out_actual_handles_ == nullptr) {
            SetError("Cannot encode with null out_actual_handles");
            return;
        }
        Super::Walk();
        if (status_ == ZX_OK) {
            *out_actual_handles_ = handle_idx();
        }
    }

    uint8_t* bytes() const { return bytes_; }
    uint32_t num_bytes() const { return num_bytes_; }
    uint32_t num_handles() const { return num_handles_; }

    bool ValidateOutOfLineStorageClaim(const void* a, const void* b) {
        return a == b;
    }

    void UnclaimedHandle(zx_handle_t* out_handle) {
#ifdef __Fuchsia__
        // Return value intentionally ignored: this is best-effort cleanup.
        zx_handle_close(*out_handle);
#endif
    }
    void ClaimedHandle(zx_handle_t* out_handle, uint32_t idx) {
        assert(out_handle != nullptr);
        handles_[idx] = *out_handle;
        *out_handle = FIDL_HANDLE_PRESENT;
    }

    PointerState GetPointerState(const void* ptr) const {
        return *static_cast<const uintptr_t*>(ptr) == 0
                   ? PointerState::ABSENT
                   : PointerState::PRESENT;
    }
    HandleState GetHandleState(zx_handle_t p) const {
        return p == ZX_HANDLE_INVALID
                   ? HandleState::ABSENT
                   : HandleState::PRESENT;
    }

    template <class T>
    void UpdatePointer(T** p, T* v) {
        assert(*p == v);
        assert(v != nullptr);
        *p = reinterpret_cast<T*>(FIDL_ALLOC_PRESENT);
    }

    void SetError(const char* error_msg) {
        if (status_ != ZX_OK) {
            return;
        }
        status_ = ZX_ERR_INVALID_ARGS;
        if (out_error_msg_ != nullptr) {
            *out_error_msg_ = error_msg;
        }
#ifdef __Fuchsia__
        if (handles_) {
            // Return value intentionally ignored: this is best-effort cleanup.
            zx_handle_close_many(handles_, num_handles());
        }
#endif
    }

    zx_status_t status() const { return status_; }

private:
    // Message state passed in to the constructor.
    uint8_t* const bytes_;
    const uint32_t num_bytes_;
    zx_handle_t* const handles_;
    const uint32_t num_handles_;
    uint32_t* const out_actual_handles_;
    const char** const out_error_msg_;
    zx_status_t status_ = ZX_OK;
};

} // namespace

zx_status_t fidl_encode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        zx_handle_t* handles, uint32_t max_handles, uint32_t* out_actual_handles,
                        const char** out_error_msg) {
    FidlEncoder encoder(type, bytes, num_bytes, handles, max_handles, out_actual_handles,
                        out_error_msg);
    encoder.Walk();
    return encoder.status();
}

zx_status_t fidl_encode_msg(const fidl_type_t* type, fidl_msg_t* msg,
                            uint32_t* out_actual_handles, const char** out_error_msg) {
    return fidl_encode(type, msg->bytes, msg->num_bytes, msg->handles, msg->num_handles,
                       out_actual_handles, out_error_msg);
}
