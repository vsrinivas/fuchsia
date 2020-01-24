// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/envelope_frames.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/visitor.h>
#include <lib/fidl/walker.h>
#include <stdalign.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <cstdint>
#include <cstdlib>
#include <limits>

namespace {

struct Position;

struct StartingPoint {
  // The starting object of linearization
  void* const source;
  // The starting address of a contiguous destination buffer
  uint8_t* const destination;
  Position ToPosition() const;
};

struct Position {
  // |object| points to one of the objects from the source pile
  void* object;
  // |offset| is an offset into the destination buffer
  uint32_t offset;
  Position operator+(uint32_t size) const {
    return Position{.object = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(object) + size),
                    .offset = offset + size};
  }
  Position& operator+=(uint32_t size) {
    *this = *this + size;
    return *this;
  }
  // By default, return the pointer in the destination buffer
  template <typename T>
  constexpr T* Get(StartingPoint start) const {
    return reinterpret_cast<T*>(start.destination + offset);
  }
  // Additional method to get a pointer to one of the source objects
  template <typename T>
  constexpr T* GetFromSource() const {
    return reinterpret_cast<T*>(object);
  }
};

Position StartingPoint::ToPosition() const { return Position{.object = source, .offset = 0}; }

using EnvelopeState = ::fidl::EnvelopeFrames::EnvelopeState;

class FidlLinearizer final
    : public fidl::Visitor<fidl::MutatingVisitorTrait, StartingPoint, Position> {
 public:
  FidlLinearizer(void* bytes, uint32_t num_bytes, uint32_t next_out_of_line,
                 const char** out_error_msg)
      : bytes_(static_cast<uint8_t*>(bytes)),
        num_bytes_(num_bytes),
        next_out_of_line_(next_out_of_line),
        out_error_msg_(out_error_msg) {}

  using StartingPoint = StartingPoint;

  using Position = Position;

  // Does not make sense to keep going after any error, since the resulting buffer
  // would not be usable anyways.
  static constexpr bool kContinueAfterConstraintViolation = false;

  // When we encounter a non-nullable vector/string with zero count, do not check the
  // data pointer. It is cumbersome for the caller to provide a meaningful value other than NULL
  // in the case of an empty vector/string.
  static constexpr bool kAllowNonNullableCollectionsToBeAbsent = true;

  Status VisitPointer(Position ptr_position, ObjectPointerPointer object_ptr_ptr,
                      uint32_t inline_size, Position* out_position) {
    // This will be mandatory in the future with LLCPP builders. Asserting now to ease the
    // migration.
    // TODO(fxb/42059) Remove this assertion after switching objects to tracking_ptr.
    assert(((reinterpret_cast<uintptr_t>(object_ptr_ptr) & 0x1) == 0) &&
           "LLCPP pointers must have least significant bit of 0. "
           "Please use at least 2-byte alignment.");

    uint32_t new_offset;
    if (!FidlAddOutOfLine(next_out_of_line_, inline_size, &new_offset)) {
      SetError("out-of-line offset overflow trying to linearize");
      return Status::kMemoryError;
    }

    if (new_offset > num_bytes_) {
      SetError("object is too big to linearize into provided buffer", ZX_ERR_BUFFER_TOO_SMALL);
      return Status::kConstraintViolationError;
    }

    // Copy the pointee to the desired location in secondary storage
    memcpy(&bytes_[next_out_of_line_], *object_ptr_ptr, inline_size);

    // Instruct the walker to traverse the pointee afterwards.
    *out_position = Position{.object = *object_ptr_ptr, .offset = next_out_of_line_};

    // Update the pointer within message buffer to point to the copy
    *object_ptr_ptr = &bytes_[next_out_of_line_];
    next_out_of_line_ = new_offset;
    return Status::kSuccess;
  }

  Status VisitHandle(Position handle_position, HandlePointer handle_ptr) {
    // Remember the address of the handle in the original objects,
    // such that after the entire tree is cloned into the contiguous buffer,
    // we can clear out the handles in the original tree in one fell swoop.
    if (handle_idx_ == ZX_CHANNEL_MAX_MSG_HANDLES) {
      SetError("too many handles when linearizing");
      return Status::kConstraintViolationError;
    }
    original_handles_[handle_idx_] = handle_position.GetFromSource<zx_handle_t>();
    handle_idx_ += 1;
    return Status::kSuccess;
  }

  Status VisitInternalPadding(Position padding_position, uint32_t padding_length) {
    return Status::kSuccess;
  }

  Status EnterEnvelope(Position envelope_position, EnvelopePointer envelope,
                       const fidl_type_t* payload_type) {
    if (envelope->data != nullptr && payload_type == nullptr) {
      SetError("Cannot linearize envelope without a coding table");
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
    // Now that the envelope has been consumed, go back and update the envelope header with
    // the correct num_bytes and num_handles values
    auto& starting_state = envelope_frames_.Pop();
    uint32_t num_bytes = next_out_of_line_ - starting_state.bytes_so_far;
    uint32_t num_handles = handle_idx_ - starting_state.handles_so_far;
    envelope->num_bytes = num_bytes;
    envelope->num_handles = num_handles;
    return Status::kSuccess;
  }

  void OnError(const char* error) { SetError(error); }

  template <typename Callback>
  void ForEachHandle(Callback cb) {
    for (uint32_t i = 0; i < handle_idx_; i++) {
      cb(original_handles_[i]);
    }
  }

  zx_status_t status() const { return status_; }

  uint32_t next_out_of_line() const { return next_out_of_line_; }

 private:
  void SetError(const char* error, zx_status_t code = ZX_ERR_INVALID_ARGS) {
    if (status_ == ZX_OK) {
      status_ = code;
      if (out_error_msg_ != nullptr) {
        *out_error_msg_ = error;
      }
    }
  }

  // Message state passed into the constructor.
  uint8_t* const bytes_;
  const uint32_t num_bytes_;
  uint32_t next_out_of_line_;
  const char** const out_error_msg_;

  // Linearizer state
  zx_status_t status_ = ZX_OK;
  uint32_t handle_idx_ = 0;
  zx_handle_t* original_handles_[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl::EnvelopeFrames envelope_frames_;
};

}  // namespace

zx_status_t fidl_linearize(const fidl_type_t* type, void* value, uint8_t* buffer,
                           uint32_t num_bytes, uint32_t* out_num_bytes,
                           const char** out_error_msg) {
  auto set_error = [&out_error_msg](const char* msg) {
    if (out_error_msg)
      *out_error_msg = msg;
  };
  if (value == nullptr) {
    set_error("Cannot linearize with null starting object");
    return ZX_ERR_INVALID_ARGS;
  }
  if (buffer == nullptr) {
    set_error("Cannot linearize with null destination buffer");
    return ZX_ERR_INVALID_ARGS;
  }
  if (!FidlIsAligned(buffer)) {
    set_error("Destination buffer must be aligned to FIDL_ALIGNMENT");
    return ZX_ERR_INVALID_ARGS;
  }

  size_t primary_size;
  zx_status_t status;
  if ((status = fidl::PrimaryObjectSize(type, &primary_size, out_error_msg)) != ZX_OK) {
    return status;
  }
  if (primary_size > num_bytes) {
    set_error("Buffer is too small for first inline object");
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  uint64_t next_out_of_line = FidlAlign(static_cast<uint32_t>(primary_size));
  if (next_out_of_line > std::numeric_limits<uint32_t>::max()) {
    set_error("Out of line starting offset overflows");
    return ZX_ERR_INVALID_ARGS;
  }

  // Copy the primary object
  memcpy(buffer, value, primary_size);

  // Zero the padding gaps
  memset(buffer + primary_size, 0, next_out_of_line - primary_size);

  FidlLinearizer linearizer(buffer, num_bytes, static_cast<uint32_t>(next_out_of_line),
                            out_error_msg);
  fidl::Walk(linearizer, type,
             StartingPoint{
                 .source = value,
                 .destination = buffer,
             });

  if (linearizer.status() != ZX_OK) {
    return linearizer.status();
  }

  // Clear out handles in the original objects
  linearizer.ForEachHandle([](zx_handle_t* handle_ptr) { *handle_ptr = ZX_HANDLE_INVALID; });

  // Return the message size, which is the starting offset of the next out-of-line object
  if (out_num_bytes) {
    *out_num_bytes = linearizer.next_out_of_line();
  }

  return ZX_OK;
}
