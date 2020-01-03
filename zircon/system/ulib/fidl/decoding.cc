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
#include <cstring>

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

constexpr uintptr_t kAllocPresenceMarker = FIDL_ALLOC_PRESENT;
constexpr uintptr_t kAllocAbsenceMarker = FIDL_ALLOC_ABSENT;

using EnvelopeState = ::fidl::EnvelopeFrames::EnvelopeState;

class FidlDecoder final
    : public fidl::Visitor<fidl::MutatingVisitorTrait, StartingPoint, Position> {
 public:
  FidlDecoder(void* bytes, uint32_t num_bytes, const zx_handle_t* handles, uint32_t num_handles,
              uint32_t next_out_of_line, const char** out_error_msg)
      : bytes_(static_cast<uint8_t*>(bytes)),
        num_bytes_(num_bytes),
        handles_(handles),
        num_handles_(num_handles),
        next_out_of_line_(next_out_of_line),
        out_error_msg_(out_error_msg) {}

  using StartingPoint = StartingPoint;

  using Position = Position;

  static constexpr bool kContinueAfterConstraintViolation = false;

  static constexpr bool kAllowNonNullableCollectionsToBeAbsent = false;

  Status VisitPointer(Position ptr_position, ObjectPointerPointer object_ptr_ptr,
                      uint32_t inline_size, Position* out_position) {
    if (reinterpret_cast<uintptr_t>(*object_ptr_ptr) != kAllocPresenceMarker) {
      SetError("decoder encountered invalid pointer");
      return Status::kConstraintViolationError;
    }
    uint32_t new_offset;
    if (!FidlAddOutOfLine(next_out_of_line_, inline_size, &new_offset)) {
      SetError("overflow updating out-of-line offset");
      return Status::kMemoryError;
    }
    if (new_offset > num_bytes_) {
      SetError("message tried to decode more than provided number of bytes");
      return Status::kMemoryError;
    }
    auto status = ValidatePadding(&bytes_[next_out_of_line_ + inline_size],
                                  new_offset - next_out_of_line_ - inline_size);
    if (status != Status::kSuccess) {
      return status;
    }
    *out_position = Position{next_out_of_line_};
    *object_ptr_ptr = reinterpret_cast<void*>(&bytes_[next_out_of_line_]);

    next_out_of_line_ = new_offset;
    return Status::kSuccess;
  }

  Status VisitHandle(Position handle_position, HandlePointer handle) {
    if (*handle != FIDL_HANDLE_PRESENT) {
      SetError("message tried to decode a garbage handle");
      return Status::kConstraintViolationError;
    }
    if (handle_idx_ == num_handles_) {
      SetError("message decoded too many handles");
      return Status::kConstraintViolationError;
    }
    if (handles_ == nullptr) {
      SetError("decoder noticed a handle is present but the handle table is empty");
      *handle = ZX_HANDLE_INVALID;
      return Status::kConstraintViolationError;
    }
    if (handles_[handle_idx_] == ZX_HANDLE_INVALID) {
      SetError("invalid handle detected in handle table");
      return Status::kConstraintViolationError;
    }
    *handle = handles_[handle_idx_];
    handle_idx_++;
    return Status::kSuccess;
  }

  Status VisitInternalPadding(Position padding_position, uint32_t padding_length) {
    auto padding_ptr = padding_position.template Get<const uint8_t>(StartingPoint{bytes_});
    return ValidatePadding(padding_ptr, padding_length);
  }

  Status EnterEnvelope(Position envelope_position, EnvelopePointer envelope,
                       const fidl_type_t* payload_type) {
    if (envelope->presence == kAllocAbsenceMarker &&
        (envelope->num_bytes != 0 || envelope->num_handles != 0)) {
      SetError("Envelope has absent data pointer, yet has data and/or handles");
      return Status::kConstraintViolationError;
    }
    if (envelope->presence != kAllocAbsenceMarker && envelope->num_bytes == 0) {
      SetError("Envelope has present data pointer, but zero byte count");
      return Status::kConstraintViolationError;
    }
    uint32_t expected_handle_count;
    if (add_overflow(handle_idx_, envelope->num_handles, &expected_handle_count) ||
        expected_handle_count > num_handles_) {
      SetError("Envelope has more handles than expected");
      return Status::kConstraintViolationError;
    }
    // Remember the current watermark of bytes and handles, so that after processing
    // the envelope, we can validate that the claimed num_bytes/num_handles matches the reality.
    if (!envelope_frames_.Push(EnvelopeState(next_out_of_line_, handle_idx_))) {
      SetError("Overly deep nested envelopes");
      return Status::kConstraintViolationError;
    }
    // If we do not have the coding table for this payload,
    // treat it as unknown and close its contained handles
    if (envelope->presence != kAllocAbsenceMarker && payload_type == nullptr &&
        envelope->num_handles > 0) {
      memcpy(&unknown_handles_[unknown_handle_idx_], &handles_[handle_idx_],
             envelope->num_handles * sizeof(zx_handle_t));
      handle_idx_ += envelope->num_handles;
      unknown_handle_idx_ += envelope->num_handles;
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

  bool DidConsumeAllBytes() const { return next_out_of_line_ == num_bytes_; }

  bool DidConsumeAllHandles() const { return handle_idx_ == num_handles_; }

  uint32_t unknown_handle_idx() const { return unknown_handle_idx_; }

  const zx_handle_t* unknown_handles() const { return unknown_handles_; }

 private:
  void SetError(const char* error) {
    if (status_ != ZX_OK) {
      return;
    }
    status_ = ZX_ERR_INVALID_ARGS;
    if (!out_error_msg_) {
      return;
    }
    *out_error_msg_ = error;
  }

  Status ValidatePadding(const uint8_t* padding_ptr, uint32_t padding_length) {
    for (uint32_t i = 0; i < padding_length; i++) {
      if (padding_ptr[i] != 0) {
        SetError("non-zero padding bytes detected during decoding");
        return Status::kConstraintViolationError;
      }
    }
    return Status::kSuccess;
  }

  // Message state passed in to the constructor.
  uint8_t* const bytes_;
  const uint32_t num_bytes_;
  const zx_handle_t* const handles_;
  const uint32_t num_handles_;
  uint32_t next_out_of_line_;
  const char** const out_error_msg_;

  // Decoder state
  zx_status_t status_ = ZX_OK;
  uint32_t handle_idx_ = 0;
  uint32_t unknown_handle_idx_ = 0;
  zx_handle_t unknown_handles_[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl::EnvelopeFrames envelope_frames_;
};

}  // namespace

zx_status_t fidl_decode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        const zx_handle_t* handles, uint32_t num_handles,
                        const char** out_error_msg) {
  auto drop_all_handles = [&]() {
#ifdef __Fuchsia__
    // Return value intentionally ignored. This is best-effort cleanup.
    (void)zx_handle_close_many(handles, num_handles);
#endif
  };
  auto set_error = [&out_error_msg](const char* msg) {
    if (out_error_msg)
      *out_error_msg = msg;
  };
  if (handles == nullptr && num_handles != 0) {
    set_error("Cannot provide non-zero handle count and null handle pointer");
    return ZX_ERR_INVALID_ARGS;
  }
  if (bytes == nullptr) {
    set_error("Cannot decode null bytes");
    drop_all_handles();
    return ZX_ERR_INVALID_ARGS;
  }
  if (!FidlIsAligned(reinterpret_cast<uint8_t*>(bytes))) {
    set_error("Bytes must be aligned to FIDL_ALIGNMENT");
    drop_all_handles();
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t next_out_of_line;
  zx_status_t status;
  if ((status = fidl::StartingOutOfLineOffset(type, num_bytes, &next_out_of_line, out_error_msg)) !=
      ZX_OK) {
    drop_all_handles();
    return status;
  }

  FidlDecoder decoder(bytes, num_bytes, handles, num_handles, next_out_of_line, out_error_msg);
  fidl::Walk(decoder, type, StartingPoint{reinterpret_cast<uint8_t*>(bytes)});

  if (decoder.status() != ZX_OK) {
    drop_all_handles();
    return decoder.status();
  }
  if (!decoder.DidConsumeAllBytes()) {
    set_error("message did not decode all provided bytes");
    drop_all_handles();
    return ZX_ERR_INVALID_ARGS;
  }
  if (!decoder.DidConsumeAllHandles()) {
    set_error("message did not decode all provided handles");
    drop_all_handles();
    return ZX_ERR_INVALID_ARGS;
  }

#ifdef __Fuchsia__
  if (decoder.unknown_handle_idx() > 0) {
    (void)zx_handle_close_many(decoder.unknown_handles(), decoder.unknown_handle_idx());
  }
#endif
  return ZX_OK;
}

zx_status_t fidl_decode_msg(const fidl_type_t* type, fidl_msg_t* msg, const char** out_error_msg) {
  return fidl_decode(type, msg->bytes, msg->num_bytes, msg->handles, msg->num_handles,
                     out_error_msg);
}
