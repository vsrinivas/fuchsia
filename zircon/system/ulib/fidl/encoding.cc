// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/envelope_frames.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/visitor.h>
#include <lib/fidl/walker.h>
#include <lib/fit/variant.h>
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
        max_handles_(max_handles),
        next_out_of_line_(next_out_of_line),
        out_error_msg_(out_error_msg) {
    if (handles != nullptr) {
      handles_ = handles;
    }
  }

  FidlEncoder(void* bytes, uint32_t num_bytes, zx_handle_disposition_t* handle_dispositions,
              uint32_t max_handle_dispositions, uint32_t next_out_of_line,
              const char** out_error_msg)
      : bytes_(static_cast<uint8_t*>(bytes)),
        num_bytes_(num_bytes),
        max_handles_(max_handle_dispositions),
        next_out_of_line_(next_out_of_line),
        out_error_msg_(out_error_msg) {
    if (handle_dispositions != nullptr) {
      handles_ = handle_dispositions;
    }
  }

  using StartingPoint = StartingPoint;

  using Position = Position;

  static constexpr bool kContinueAfterConstraintViolation = true;

  static constexpr bool kAllowNonNullableCollectionsToBeAbsent = false;

  Status VisitPointer(Position ptr_position, PointeeType pointee_type,
                      ObjectPointerPointer object_ptr_ptr, uint32_t inline_size,
                      Position* out_position) {
    // Make sure objects in secondary storage are contiguous
    if (!ClaimOutOfLineStorage(static_cast<uint32_t>(inline_size), *object_ptr_ptr, out_position)) {
      return Status::kMemoryError;
    }
    // Rewrite pointer as "present" placeholder
    *object_ptr_ptr = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
    return Status::kSuccess;
  }

  Status VisitHandle(Position handle_position, HandlePointer handle, zx_rights_t handle_rights,
                     zx_obj_type_t handle_subtype) {
    if (handle_idx_ == max_handles_) {
      SetError("message tried to encode too many handles");
      ThrowAwayHandle(handle);
      return Status::kConstraintViolationError;
    }

    if (has_handles()) {
      handles()[handle_idx_] = *handle;
    } else if (has_handle_dispositions()) {
      handle_dispositions()[handle_idx_] = zx_handle_disposition_t{
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = *handle,
          .type = handle_subtype,
          .rights = handle_rights,
          .result = ZX_OK,
      };
    } else {
      SetError("did not provide place to store handles");
      ThrowAwayHandle(handle);
      return Status::kConstraintViolationError;
    }

    *handle = FIDL_HANDLE_PRESENT;
    handle_idx_++;
    return Status::kSuccess;
  }

  Status VisitVectorOrStringCount(CountPointer ptr) { return Status::kSuccess; }

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

  bool has_handles() const { return fit::holds_alternative<zx_handle_t*>(handles_); }
  bool has_handle_dispositions() const {
    return fit::holds_alternative<zx_handle_disposition_t*>(handles_);
  }
  zx_handle_t* handles() const { return fit::get<zx_handle_t*>(handles_); }
  zx_handle_disposition_t* handle_dispositions() const {
    return fit::get<zx_handle_disposition_t*>(handles_);
  }

  // Message state passed in to the constructor.
  uint8_t* const bytes_;
  const uint32_t num_bytes_;
  fit::variant<fit::monostate, zx_handle_t*, zx_handle_disposition_t*> handles_;
  const uint32_t max_handles_;
  uint32_t next_out_of_line_;
  const char** const out_error_msg_;

  // Encoder state
  zx_status_t status_ = ZX_OK;
  uint32_t handle_idx_ = 0;
  fidl::EnvelopeFrames envelope_frames_;
};

template <typename HandleType>
zx_status_t fidl_encode_impl(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                             HandleType* handles, uint32_t max_handles,
                             uint32_t* out_actual_handles, const char** out_error_msg,
                             void (*close_handles)(const HandleType*, uint32_t)) {
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
    close_handles(handles, encoder.handle_idx());
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

void close_handles_op(const zx_handle_t* handles, uint32_t max_idx) {
#ifdef __Fuchsia__
  if (handles) {
    // Return value intentionally ignored. This is best-effort cleanup.
    zx_handle_close_many(handles, max_idx);
  }
#endif
}

void close_handle_dispositions_op(const zx_handle_disposition_t* handle_dispositions,
                                  uint32_t max_idx) {
#ifdef __Fuchsia__
  if (handle_dispositions) {
    zx_handle_t* handles = reinterpret_cast<zx_handle_t*>(alloca(sizeof(zx_handle_t) * max_idx));
    for (uint32_t i = 0; i < max_idx; i++) {
      handles[i] = handle_dispositions[i].handle;
    }
    // Return value intentionally ignored. This is best-effort cleanup.
    zx_handle_close_many(handles, max_idx);
  }
#endif
}

}  // namespace

zx_status_t fidl_encode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        zx_handle_t* handles, uint32_t max_handles, uint32_t* out_actual_handles,
                        const char** out_error_msg) {
  return fidl_encode_impl(type, bytes, num_bytes, handles, max_handles, out_actual_handles,
                          out_error_msg, close_handles_op);
}

zx_status_t fidl_encode_etc(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                            zx_handle_disposition_t* handle_dispositions,
                            uint32_t max_handle_dispositions, uint32_t* out_actual_handles,
                            const char** out_error_msg) {
  return fidl_encode_impl(type, bytes, num_bytes, handle_dispositions, max_handle_dispositions,
                          out_actual_handles, out_error_msg, close_handle_dispositions_op);
}

zx_status_t fidl_encode_msg(const fidl_type_t* type, fidl_msg_t* msg, uint32_t* out_actual_handles,
                            const char** out_error_msg) {
  return fidl_encode(type, msg->bytes, msg->num_bytes, msg->handles, msg->num_handles,
                     out_actual_handles, out_error_msg);
}
