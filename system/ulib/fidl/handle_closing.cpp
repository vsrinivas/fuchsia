// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <limits>
#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>

#include <lib/fidl/coding.h>

#ifdef __Fuchsia__

#include <lib/fidl/internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>

#include "buffer_walker.h"

namespace {

class FidlHandleCloser final : public fidl::internal::BufferWalker<FidlHandleCloser, true, true> {
    typedef fidl::internal::BufferWalker<FidlHandleCloser, true, true> Super;

public:
    FidlHandleCloser(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                     const char** out_error_msg)
        : Super(type), bytes_(static_cast<uint8_t*>(bytes)), num_bytes_(num_bytes),
          out_error_msg_(out_error_msg) {}

    void Walk() {
        Super::Walk();
    }

    uint8_t* bytes() const { return bytes_; }
    uint32_t num_bytes() const { return num_bytes_; }
    uint32_t num_handles() const { return std::numeric_limits<uint32_t>::max(); }

    bool ValidateOutOfLineStorageClaim(const void* a, void* b) {
        return true;
    }

    void UnclaimedHandle(zx_handle_t* out_handle) {
        // This will never happen since we are returning numeric_limits::max() in num_handles.
        // We want to claim (close) all the handles.
        ZX_DEBUG_ASSERT(false);
    }

    void ClaimedHandle(zx_handle_t* out_handle, uint32_t idx) {
        if (*out_handle != ZX_HANDLE_INVALID) {
            zx_handle_close(*out_handle);
        }
        *out_handle = ZX_HANDLE_INVALID;
    }

    template <class T>
    void UpdatePointer(T* const* p, T* v) {}

    PointerState GetPointerState(const void* ptr) const {
        return *static_cast<const uintptr_t*>(ptr) == 0
               ? PointerState::ABSENT
               : PointerState::PRESENT;
    }

    HandleState GetHandleState(zx_handle_t p) const {
        // Treat all handles as present to keep the buffer walker going.
        return HandleState::PRESENT;
    }

    void SetError(const char* error_msg) {
        status_ = ZX_ERR_INVALID_ARGS;
        if (out_error_msg_ != nullptr) {
            *out_error_msg_ = error_msg;
        }
    }

    zx_status_t status() const { return status_; }

private:
    // Message state passed in to the constructor.
    uint8_t* const bytes_;
    const uint32_t num_bytes_;
    const char** const out_error_msg_;
    zx_status_t status_ = ZX_OK;
};

} // namespace

#endif  // __Fuchsia__

zx_status_t fidl_close_handles(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                               const char** out_error_msg) {
#if __Fuchsia__
    FidlHandleCloser handle_closer(type, bytes, num_bytes, out_error_msg);
    handle_closer.Walk();
    return handle_closer.status();
#else
    return ZX_OK;  // there can't be any handles on the host
#endif
}

zx_status_t fidl_close_handles_msg(const fidl_type_t* type, const fidl_msg_t* msg,
                                   const char** out_error_msg) {
    return fidl_close_handles(type, msg->bytes, msg->num_bytes, out_error_msg);
}

