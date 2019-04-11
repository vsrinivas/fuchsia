// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/embedded/decoder.h"

#include <lib/fidl/envelope_frames.h>
#include <lib/fidl/walker.h>

#include "src/connectivity/overnet/lib/embedded/fidl_channel.h"

namespace overnet {
namespace internal {

Decoder::Decoder(fuchsia::overnet::protocol::ZirconChannelMessage message,
                 FidlChannelIO* fidl_channel_io)
    : message_(std::move(message)),
      stream_(fidl_channel_io->channel()->overnet_stream()) {}

size_t Decoder::GetOffset(void* ptr) {
  return GetOffset(reinterpret_cast<uintptr_t>(ptr));
}

size_t Decoder::GetOffset(uintptr_t ptr) {
  // The |ptr| value comes from the message buffer, which we've already
  // validated. That means it should coorespond to a valid offset within the
  // message.
  return ptr - reinterpret_cast<uintptr_t>(message_.bytes.data());
}

///////////////////////////////////////////////////////////////////////////////
// The remainder of this file implements Decoder::FidlDecode

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

class FidlDecoder final : public fidl::Visitor<fidl::MutatingVisitorTrait,
                                               StartingPoint, Position> {
 public:
  FidlDecoder(std::vector<uint8_t>* bytes,
              std::vector<fuchsia::overnet::protocol::ZirconHandle>* handles,
              uint32_t next_out_of_line, const char** out_error_msg)
      : bytes_(bytes),
        handles_(handles),
        next_out_of_line_(next_out_of_line),
        out_error_msg_(out_error_msg) {}

  using StartingPoint = StartingPoint;

  using Position = Position;

  static constexpr bool kContinueAfterConstraintViolation = true;

  Status VisitPointer(Position ptr_position,
                      ObjectPointerPointer object_ptr_ptr, uint32_t inline_size,
                      Position* out_position) {
    if (reinterpret_cast<uintptr_t>(*object_ptr_ptr) != kAllocPresenceMarker) {
      SetError("decoder encountered invalid pointer");
      return Status::kConstraintViolationError;
    }
    uint32_t new_offset;
    if (!fidl::AddOutOfLine(next_out_of_line_, inline_size, &new_offset)) {
      SetError("overflow updating out-of-line offset");
      return Status::kMemoryError;
    }
    if (new_offset > bytes_->size()) {
      SetError("message tried to decode more than provided number of bytes");
      return Status::kMemoryError;
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
    if (handle_idx_ == handles_->size()) {
      SetError("message decoded too many handles");
      return Status::kConstraintViolationError;
    }
    if (handles_ == nullptr) {
      SetError(
          "decoder noticed a handle is present but the handle table is empty");
      *handle = ZX_HANDLE_INVALID;
      return Status::kConstraintViolationError;
    }
    if ((*handles_)[handle_idx_].Which() ==
        fuchsia::overnet::protocol::ZirconHandle::Tag::Empty) {
      SetError("invalid handle detected in handle table");
      return Status::kConstraintViolationError;
    }
    *handle = handle_idx_ + 1;
    handle_idx_++;
    return Status::kSuccess;
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
    if (add_overflow(handle_idx_, envelope->num_handles,
                     &expected_handle_count) ||
        expected_handle_count > handles_->size()) {
      SetError("Envelope has more handles than expected");
      return Status::kConstraintViolationError;
    }
    // Remember the current watermark of bytes and handles, so that after
    // processing the envelope, we can validate that the claimed
    // num_bytes/num_handles matches the reality.
    if (!envelope_frames_.Push(EnvelopeState(next_out_of_line_, handle_idx_))) {
      SetError("Overly deep nested envelopes");
      return Status::kConstraintViolationError;
    }
    // If we do not have the coding table for this payload,
    // treat it as unknown and close its contained handles
    if (envelope->presence != kAllocAbsenceMarker && payload_type == nullptr &&
        envelope->num_handles > 0) {
      handle_idx_ += envelope->num_handles;
    }
    return Status::kSuccess;
  }

  Status LeaveEnvelope(Position envelope_position, EnvelopePointer envelope) {
    // Now that the envelope has been consumed, check the correctness of the
    // envelope header.
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

  bool DidConsumeAllBytes() const {
    return next_out_of_line_ == bytes_->size();
  }

  bool DidConsumeAllHandles() const { return handle_idx_ == handles_->size(); }

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

  // Message state passed in to the constructor.
  std::vector<uint8_t>* const bytes_;
  std::vector<fuchsia::overnet::protocol::ZirconHandle>* const handles_;
  uint32_t next_out_of_line_;
  const char** const out_error_msg_;

  // Decoder state
  zx_status_t status_ = ZX_OK;
  uint32_t handle_idx_ = 0;
  fidl::EnvelopeFrames envelope_frames_;
};

}  // namespace

zx_status_t Decoder::FidlDecode(const fidl_type_t* type,
                                const char** out_error_msg) {
  auto set_error = [&out_error_msg](const char* msg) {
    if (out_error_msg)
      *out_error_msg = msg;
  };
  auto* bytes = message_.bytes.data();
  const auto num_bytes = message_.bytes.size();
  if (!fidl::IsAligned(reinterpret_cast<uint8_t*>(bytes))) {
    set_error("Bytes must be aligned to FIDL_ALIGNMENT");
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t next_out_of_line;
  zx_status_t status;
  if ((status = fidl::StartingOutOfLineOffset(
           type, num_bytes, &next_out_of_line, out_error_msg)) != ZX_OK) {
    return status;
  }

  FidlDecoder decoder(&message_.bytes, &message_.handles, next_out_of_line,
                      out_error_msg);
  fidl::Walk(decoder, type, StartingPoint{reinterpret_cast<uint8_t*>(bytes)});

  if (decoder.status() == ZX_OK) {
    if (!decoder.DidConsumeAllBytes()) {
      set_error("message did not decode all provided bytes");
      return ZX_ERR_INVALID_ARGS;
    }
    if (!decoder.DidConsumeAllHandles()) {
      set_error("message did not decode all provided handles");
      return ZX_ERR_INVALID_ARGS;
    }
  }

  return decoder.status();
}

}  // namespace internal
}  // namespace overnet