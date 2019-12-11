// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/envelope_frames.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/visitor.h>
#include <lib/fidl/walker.h>
#include <stdalign.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <cstdint>
#include <cstdlib>
#include <limits>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

// TODO(kulakowski) Design zx_status_t error values.

namespace {

struct Position;

struct StartingPoint {
  uint8_t* const addr;
  Position ToPosition() const;
};

struct Position {
  uint32_t offset;
  Position operator+(uint32_t size) const { return Position{offset + size}; }
  Position& operator+=(uint32_t size) {
    offset += size;
    return *this;
  }
  template <typename T>
  constexpr T* Get(StartingPoint start) const {
    return reinterpret_cast<T*>(start.addr + offset);
  }
};

Position StartingPoint::ToPosition() const { return Position{0}; }

using EnvelopeState = ::fidl::EnvelopeFrames::EnvelopeState;

class FidlEncoder final
    : public fidl::Visitor<fidl::MutatingVisitorTrait, StartingPoint, Position> {
 public:
  FidlEncoder(void* bytes, uint32_t num_bytes, zx_handle_t* handles, uint32_t max_handles,
              uint32_t next_out_of_line, const char** out_error_msg)
      : bytes_(static_cast<uint8_t*>(bytes)),
        num_bytes_(num_bytes),
        handles_(handles),
        max_handles_(max_handles),
        next_out_of_line_(next_out_of_line),
        out_error_msg_(out_error_msg) {}

  using StartingPoint = StartingPoint;

  using Position = Position;

  static constexpr bool kContinueAfterConstraintViolation = true;

  static constexpr bool kAllowNonNullableCollectionsToBeAbsent = false;

  Status VisitPointer(Position ptr_position, ObjectPointerPointer object_ptr_ptr,
                      uint32_t inline_size, Position* out_position) {
    // Make sure objects in secondary storage are contiguous
    if (!ClaimOutOfLineStorage(static_cast<uint32_t>(inline_size), *object_ptr_ptr, out_position)) {
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
    handle_idx_++;
    return Status::kSuccess;
  }

  Status VisitInternalPadding(Position padding_position, uint32_t padding_length) {
    auto padding_ptr = padding_position.template Get<uint8_t>(StartingPoint{bytes_});
    memset(padding_ptr, 0, padding_length);
    return Status::kSuccess;
  }

  Status EnterEnvelope(Position envelope_position, EnvelopePointer envelope,
                       const fidl_type_t* payload_type) {
    // Validate envelope data/bytes invariants
    if (envelope->data == nullptr && (envelope->num_bytes != 0 || envelope->num_handles != 0)) {
      SetError("Envelope has absent data pointer, yet has data and/or handles");
      return Status::kConstraintViolationError;
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
    if (!envelope_frames_.Push(EnvelopeState(next_out_of_line_, handle_idx_))) {
      SetError("Overly deep nested envelopes");
      return Status::kConstraintViolationError;
    }
    return Status::kSuccess;
  }

  Status LeaveEnvelope(Position envelope_position, EnvelopePointer envelope) {
    // Now that the envelope has been consumed, check the correctness of the envelope header.
    auto& starting_state = envelope_frames_.Pop();
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

  void OnError(const char* error) { SetError(error); }

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
    uint32_t new_offset;
    if (!FidlAddOutOfLine(next_out_of_line_, size, &new_offset)) {
      SetError("overflow updating out-of-line offset");
      return false;
    }
    if (new_offset > num_bytes_) {
      SetError("message tried to encode more than provided number of bytes");
      return false;
    }
    // Zero the padding gaps
    memset(&bytes_[next_out_of_line_ + size], 0, new_offset - next_out_of_line_ - size);
    *out_position = Position{next_out_of_line_};
    next_out_of_line_ = new_offset;
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
  fidl::EnvelopeFrames envelope_frames_;
};

}  // namespace

zx_status_t fidl_encode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        zx_handle_t* handles, uint32_t max_handles, uint32_t* out_actual_handles,
                        const char** out_error_msg) {
  auto set_error = [&out_error_msg](const char* msg) {
    if (out_error_msg)
      *out_error_msg = msg;
  };
  if (bytes == nullptr) {
    set_error("Cannot encode null bytes");
    return ZX_ERR_INVALID_ARGS;
  }
  if (!FidlIsAligned(reinterpret_cast<uint8_t*>(bytes))) {
    set_error("Bytes must be aligned to FIDL_ALIGNMENT");
    return ZX_ERR_INVALID_ARGS;
  }
  if (num_bytes % FIDL_ALIGNMENT != 0) {
    set_error("num_bytes must be aligned to FIDL_ALIGNMENT");
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status;
  uint32_t next_out_of_line;
  if ((status = fidl::StartingOutOfLineOffset(type, num_bytes, &next_out_of_line, out_error_msg)) !=
      ZX_OK) {
    return status;
  }

  // Zero region between primary object and next out of line object.
  size_t primary_size;
  if ((status = fidl::PrimaryObjectSize(type, &primary_size, out_error_msg)) != ZX_OK) {
    return status;
  }
  memset(reinterpret_cast<uint8_t*>(bytes) + primary_size, 0, next_out_of_line - primary_size);

  FidlEncoder encoder(bytes, num_bytes, handles, max_handles, next_out_of_line, out_error_msg);
  fidl::Walk(encoder, type, StartingPoint{reinterpret_cast<uint8_t*>(bytes)});

  auto drop_all_handles = [&]() {
    if (out_actual_handles) {
      *out_actual_handles = 0;
    }
#ifdef __Fuchsia__
    if (handles) {
      // Return value intentionally ignored. This is best-effort cleanup.
      (void)zx_handle_close_many(handles, encoder.handle_idx());
    }
#endif
  };

  if (encoder.status() == ZX_OK) {
    if (!encoder.DidConsumeAllBytes()) {
      set_error("message did not encode all provided bytes");
      drop_all_handles();
      return ZX_ERR_INVALID_ARGS;
    }
    if (out_actual_handles == nullptr) {
      set_error("Cannot encode with null out_actual_handles");
      drop_all_handles();
      return ZX_ERR_INVALID_ARGS;
    }
    *out_actual_handles = encoder.handle_idx();
  } else {
    drop_all_handles();
  }

  if (handles == nullptr && max_handles != 0) {
    set_error("Cannot provide non-zero handle count and null handle pointer");
    // When |handles| is nullptr, handles are closed as part of traversal.
    return ZX_ERR_INVALID_ARGS;
  }

  return encoder.status();
}

zx_status_t fidl_encode_msg(const fidl_type_t* type, fidl_msg_t* msg, uint32_t* out_actual_handles,
                            const char** out_error_msg) {
  return fidl_encode(type, msg->bytes, msg->num_bytes, msg->handles, msg->num_handles,
                     out_actual_handles, out_error_msg);
}
