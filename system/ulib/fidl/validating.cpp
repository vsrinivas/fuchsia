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

#include "buffer_walker.h"

// TODO(kulakowski) Design zx_status_t error values.

namespace {

class FidlValidator final : public fidl::internal::BufferWalker<FidlValidator, false, false> {
    typedef fidl::internal::BufferWalker<FidlValidator, false, false> Super;

public:
    FidlValidator(const fidl_type_t* type, const void* bytes, uint32_t num_bytes,
                  uint32_t num_handles, const char** out_error_msg)
        : Super(type), bytes_(static_cast<const uint8_t*>(bytes)), num_bytes_(num_bytes),
          num_handles_(num_handles), out_error_msg_(out_error_msg) {}

    void Walk() {
        Super::Walk();
        if (status_ == ZX_OK && handle_idx() != num_handles()) {
            SetError("message did not contain the specified number of handles");
            return;
        }
    }

    const uint8_t* bytes() const { return bytes_; }
    uint32_t num_bytes() const { return num_bytes_; }
    uint32_t num_handles() const { return num_handles_; }

    bool ValidateOutOfLineStorageClaim(const void* a, const void* b) {
        return true;
    }

    void UnclaimedHandle(const zx_handle_t* out_handle) {}
    void ClaimedHandle(const zx_handle_t* out_handle, uint32_t idx) {}

    template <class T>
    void UpdatePointer(const T* const* p, const T* v) {}

    PointerState GetPointerState(const void* ptr) const {
        return static_cast<PointerState>(*static_cast<const uintptr_t*>(ptr));
    }
    HandleState GetHandleState(zx_handle_t p) const {
        return static_cast<HandleState>(p);
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
    const uint8_t* const bytes_;
    const uint32_t num_bytes_;
    const uint32_t num_handles_;
    const char** const out_error_msg_;
    zx_status_t status_ = ZX_OK;
};

} // namespace

zx_status_t fidl_validate(const fidl_type_t* type, const void* bytes, uint32_t num_bytes,
                          uint32_t num_handles, const char** out_error_msg) {
    FidlValidator validator(type, bytes, num_bytes, num_handles, out_error_msg);
    validator.Walk();
    return validator.status();
}

zx_status_t fidl_validate_msg(const fidl_type_t* type, const fidl_msg_t* msg,
                              const char** out_error_msg) {
    return fidl_validate(type, msg->bytes, msg->num_bytes, msg->num_handles,
                         out_error_msg);
}
