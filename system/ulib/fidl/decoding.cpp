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

// TODO(kulakowski) Design zx_status_t error values.

namespace {

class FidlDecoder final : public fidl::internal::BufferWalker<FidlDecoder, true, false> {
    typedef fidl::internal::BufferWalker<FidlDecoder, true, false> Super;

public:
    FidlDecoder(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                const zx_handle_t* handles, uint32_t num_handles, const char** out_error_msg)
        : Super(type), bytes_(static_cast<uint8_t*>(bytes)), num_bytes_(num_bytes),
          handles_(handles), num_handles_(num_handles), out_error_msg_(out_error_msg) {}

    void Walk() {
        if (handles_ == nullptr && num_handles_ != 0u) {
            SetError("Cannot provide non-zero handle count and null handle pointer");
            return;
        }
        Super::Walk();
        if (status_ == ZX_OK && handle_idx() != num_handles()) {
            SetError("message did not contain the specified number of handles");
        }
    }

    uint8_t* bytes() const { return bytes_; }
    uint32_t num_bytes() const { return num_bytes_; }
    uint32_t num_handles() const { return num_handles_; }

    bool ValidateOutOfLineStorageClaim(const void* a, const void* b) {
        return true;
    }

    void UnclaimedHandle(zx_handle_t* out_handle) {}
    void ClaimedHandle(zx_handle_t* out_handle, uint32_t idx) {
        if (out_handle != nullptr) {
            *out_handle = handles_[idx];
#ifdef __Fuchsia__
        } else {
            // Return value intentionally ignored: this is best-effort cleanup.
            zx_handle_close(handles_[idx]);
#endif
        }
    }

    PointerState GetPointerState(const void* ptr) const {
        return static_cast<PointerState>(*static_cast<const uintptr_t*>(ptr));
    }
    HandleState GetHandleState(zx_handle_t p) const {
        return static_cast<HandleState>(p);
    }

    template <class T>
    void UpdatePointer(T* p, T v) {
        *p = v;
    }

    void SetError(const char* error_msg) {
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
    uint8_t* const bytes_;
    const uint32_t num_bytes_;
    const zx_handle_t* const handles_;
    const uint32_t num_handles_;
    const char** const out_error_msg_;
    zx_status_t status_ = ZX_OK;
};

} // namespace

zx_status_t fidl_decode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        const zx_handle_t* handles, uint32_t num_handles,
                        const char** out_error_msg) {
    FidlDecoder decoder(type, bytes, num_bytes, handles, num_handles, out_error_msg);
    decoder.Walk();
    return decoder.status();
}

zx_status_t fidl_decode_msg(const fidl_type_t* type, fidl_msg_t* msg,
                            const char** out_error_msg) {
    return fidl_decode(type, msg->bytes, msg->num_bytes, msg->handles,
                       msg->num_handles, out_error_msg);
}
