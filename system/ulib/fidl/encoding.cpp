// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdalign.h>

#include <lib/fidl/internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

#include "visitor.h"
#include "walker.h"

// TODO(kulakowski) Design zx_status_t error values.

namespace {

struct Position;

struct StartingPoint {
    uint8_t* const addr;
    Position ToPosition() const;
};

struct Position {
    uint32_t offset;
    Position operator+(uint32_t size) const {
        return Position { offset + size };
    }
    Position& operator+=(uint32_t size) {
        offset += size;
        return *this;
    }
    template <typename T>
    constexpr T* Get(StartingPoint start) const {
        return reinterpret_cast<T*>(start.addr + offset);
    }
};

Position StartingPoint::ToPosition() const {
    return Position { 0 };
}

class FidlEncoder final : public fidl::Visitor<
    fidl::MutatingVisitorTrait, StartingPoint, Position> {
public:
    FidlEncoder(void* bytes, uint32_t num_bytes, zx_handle_t* handles, uint32_t max_handles,
                uint32_t next_out_of_line, const char** out_error_msg)
        : bytes_(static_cast<uint8_t*>(bytes)), num_bytes_(num_bytes),
          handles_(handles), max_handles_(max_handles), next_out_of_line_(next_out_of_line),
          out_error_msg_(out_error_msg) {}

    using StartingPoint = StartingPoint;

    using Position = Position;

    static constexpr bool kContinueAfterConstraintViolation = true;

    Status VisitPointer(Position ptr_position,
                        ObjectPointerPointer object_ptr_ptr,
                        uint32_t inline_size,
                        Position* out_position) {
        if (inline_size > std::numeric_limits<uint32_t>::max()) {
            SetError("inline size is too big");
            return Status::kMemoryError;
        }
        // Make sure objects in secondary storage are contiguous
        if (!ClaimOutOfLineStorage(static_cast<uint32_t>(inline_size),
                                   *object_ptr_ptr,
                                   out_position)) {
            return Status::kMemoryError;
        }
        // Rewrite pointer as "present" placeholder
        *object_ptr_ptr = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
        return Status::kSuccess;
    }

    Status VisitHandle(Position handle_position, HandlePointer handle) {
        if (handle_idx_ == max_handles_) {
            SetError("message tried to encode too many handles");
            ThrowAwayHandle(handle);
            return Status::kConstraintViolationError;
        }
        if (handles_ == nullptr) {
            SetError("did not provide place to store handles");
            ThrowAwayHandle(handle);
            return Status::kConstraintViolationError;
        }
        handles_[handle_idx_] = *handle;
        *handle = FIDL_HANDLE_PRESENT;
        handle_idx_ += 1;
        return Status::kSuccess;
    }

    Status EnterEnvelope(Position envelope_position,
                         EnvelopePointer envelope,
                         const fidl_type_t* payload_type) {
        // Validate envelope data/bytes invariants
        if (envelope->data == nullptr) {
            if (!(envelope->num_bytes == 0 && envelope->num_handles == 0)) {
                SetError("Envelope has absent data pointer, yet has data and/or handles");
                return Status::kConstraintViolationError;
            }
        }
        if (envelope->data != nullptr && envelope->num_bytes == 0) {
            SetError("Envelope has present data pointer, but zero byte count");
            return Status::kConstraintViolationError;
        }
        if (envelope->data != nullptr && envelope->num_handles > 0 && payload_type == nullptr) {
            // Since we do not know the shape of the objects in this envelope,
            // we cannot move the handles scattered in the message.
            SetError("Does not know how to encode for this ordinal");
            return Status::kConstraintViolationError;
        }
        // Remember the current watermark of bytes and handles, so that after processing
        // the envelope, we can validate that the claimed num_bytes/num_handles matches the reality.
        if (!Push(next_out_of_line_, handle_idx_)) {
            SetError("Overly deep nested envelopes");
            return Status::kConstraintViolationError;
        }
        return Status::kSuccess;
    }

    Status LeaveEnvelope(Position envelope_position, EnvelopePointer envelope) {
        // Now that the envelope has been consumed, check the correctness of the envelope header.
        auto& starting_state = Pop();
        uint32_t num_bytes = next_out_of_line_ - starting_state.bytes_so_far;
        uint32_t num_handles = handle_idx_ - starting_state.handles_so_far;
        if (envelope->num_bytes != num_bytes) {
            SetError("Envelope num_bytes was mis-sized");
            return Status::kConstraintViolationError;
        }
        if (envelope->num_handles != num_handles) {
            SetError("Envelope num_handles was mis-sized");
            return Status::kConstraintViolationError;
        }
        return Status::kSuccess;
    }

    void OnError(const char* error) {
        SetError(error);
    }

    zx_status_t status() const { return status_; }

    uint32_t handle_idx() const { return handle_idx_; }

    bool DidConsumeAllBytes() const { return next_out_of_line_ == num_bytes_; }

private:
    void SetError(const char* error) {
        if (status_ == ZX_OK) {
            status_ = ZX_ERR_INVALID_ARGS;
            if (out_error_msg_ != nullptr) {
                *out_error_msg_ = error;
            }
        }
    }

    void ThrowAwayHandle(HandlePointer handle) {
#ifdef __Fuchsia__
        zx_handle_close(*handle);
#endif
        *handle = ZX_HANDLE_INVALID;
    }

    bool ClaimOutOfLineStorage(uint32_t size, void* storage, Position* out_position) {
        if (storage != &bytes_[next_out_of_line_]) {
            SetError("noncontiguous out of line storage during encode");
            return false;
        }
        // We have to manually maintain alignment here. For example, a pointer
        // to a struct that is 4 bytes still needs to advance the next
        // out-of-line offset by 8 to maintain the aligned-to-FIDL_ALIGNMENT
        // property.
        static constexpr uint32_t mask = FIDL_ALIGNMENT - 1;
        uint32_t new_offset = next_out_of_line_;
        if (add_overflow(new_offset, size, &new_offset)
            || add_overflow(new_offset, mask, &new_offset)) {
            SetError("overflow updating out-of-line offset");
            return false;
        }
        new_offset &= ~mask;

        if (new_offset > num_bytes_) {
            SetError("message tried to encode more than provided number of bytes");
            return false;
        }
        *out_position = Position { next_out_of_line_ };
        next_out_of_line_ = new_offset;
        return true;
    }

    struct EnvelopeState {
        uint32_t bytes_so_far;
        uint32_t handles_so_far;
    };

    const EnvelopeState& Pop() {
        ZX_ASSERT(envelope_depth_ != 0);
        envelope_depth_ -= 1;
        return envelope_states_[envelope_depth_];
    }

    bool Push(uint32_t num_bytes, uint32_t num_handles) {
        if (envelope_depth_ == FIDL_RECURSION_DEPTH) {
            return false;
        }
        envelope_states_[envelope_depth_] = (EnvelopeState) {
            .bytes_so_far = num_bytes,
            .handles_so_far = num_handles,
        };
        envelope_depth_ += 1;
        return true;
    }

    // Message state passed in to the constructor.
    uint8_t* const bytes_;
    const uint32_t num_bytes_;
    zx_handle_t* const handles_;
    const uint32_t max_handles_;
    uint32_t next_out_of_line_;
    const char** const out_error_msg_;

    // Encoder state
    zx_status_t status_ = ZX_OK;
    uint32_t handle_idx_ = 0;
    uint32_t envelope_depth_ = 0;
    EnvelopeState envelope_states_[FIDL_RECURSION_DEPTH];
};

} // namespace

zx_status_t fidl_encode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        zx_handle_t* handles, uint32_t max_handles, uint32_t* out_actual_handles,
                        const char** out_error_msg) {
    auto set_error = [&out_error_msg] (const char* msg) {
        if (out_error_msg) *out_error_msg = msg;
    };
    if (bytes == nullptr) {
        set_error("Cannot encode null bytes");
        return ZX_ERR_INVALID_ARGS;
    }
    if (handles == nullptr && max_handles != 0) {
        set_error("Cannot provide non-zero handle count and null handle pointer");
        return ZX_ERR_INVALID_ARGS;
    }
    if (out_actual_handles == nullptr) {
        set_error("Cannot encode with null out_actual_handles");
        return ZX_ERR_INVALID_ARGS;
    }
    if (type == nullptr) {
        set_error("Cannot encode a null fidl type");
        return ZX_ERR_INVALID_ARGS;
    }

    size_t primary_size;
    zx_status_t status;
    if ((status = fidl::GetPrimaryObjectSize(type, &primary_size, out_error_msg)) != ZX_OK) {
        return status;
    }
    if (primary_size > num_bytes) {
        set_error("Buffer is too small for first inline object");
        return ZX_ERR_INVALID_ARGS;
    }
    uint64_t next_out_of_line = fidl::FidlAlign(static_cast<uint32_t>(primary_size));
    if (next_out_of_line > std::numeric_limits<uint32_t>::max()) {
        set_error("Out of line starting offset overflows");
        return ZX_ERR_INVALID_ARGS;
    }

    FidlEncoder encoder(bytes, num_bytes, handles, max_handles,
                        static_cast<uint32_t>(next_out_of_line), out_error_msg);
    fidl::Walk(encoder,
               type,
               StartingPoint { reinterpret_cast<uint8_t*>(bytes) });

    if (encoder.status() == ZX_OK) {
        if (!encoder.DidConsumeAllBytes()) {
            set_error("message did not encode all provided bytes");
            return ZX_ERR_INVALID_ARGS;
        }
        *out_actual_handles = encoder.handle_idx();
    } else {
#ifdef __Fuchsia__
        if (handles) {
            // Return value intentionally ignored. This is best-effort cleanup.
            zx_handle_close_many(handles, max_handles);
        }
#endif
    }

    return encoder.status();
}

zx_status_t fidl_encode_msg(const fidl_type_t* type, fidl_msg_t* msg,
                            uint32_t* out_actual_handles, const char** out_error_msg) {
    return fidl_encode(type, msg->bytes, msg->num_bytes, msg->handles, msg->num_handles,
                       out_actual_handles, out_error_msg);
}
