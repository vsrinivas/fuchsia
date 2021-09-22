// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/visitor.h>
#include <lib/fidl/walker.h>
#include <lib/stdcompat/variant.h>
#include <stdalign.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

namespace {

struct EncodingPosition {
  // |source_object| points to one of the objects from the source pile.
  void* source_object;
  // |dest| is an address in the destination buffer.
  uint8_t* dest;
  __ALWAYS_INLINE static EncodingPosition Create(void* source_object, uint8_t* dest) {
    return {
        .source_object = source_object,
        .dest = dest,
    };
  }
  __ALWAYS_INLINE EncodingPosition operator+(uint32_t size) const {
    return {
        .source_object = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(source_object) + size),
        .dest = dest + size,
    };
  }
  __ALWAYS_INLINE EncodingPosition& operator+=(uint32_t size) {
    *this = *this + size;
    return *this;
  }
  // By default, return the pointer to the destination buffer
  template <typename T>
  __ALWAYS_INLINE constexpr T* Get() const {
    return reinterpret_cast<T*>(dest);
  }
  // Additional method to get a pointer to one of the source objects
  template <typename T>
  __ALWAYS_INLINE constexpr T* GetFromSource() const {
    return reinterpret_cast<T*>(source_object);
  }
};

struct EnvelopeCheckpoint {
  uint32_t num_bytes;
  uint32_t num_handles;
};

struct EncodeArgs {
  uint8_t* const backing_buffer;
  const uint32_t backing_buffer_capacity;
  zx_channel_iovec_t* const iovecs;
  const uint32_t iovecs_capacity;
  zx_handle_disposition_t* handles;
  const uint32_t handles_capacity;
  const uint32_t inline_object_size;
  const char** out_error_msg;
};

template <FidlWireFormatVersion WireFormatVersion>
class FidlEncoder final : public ::fidl::Visitor<WireFormatVersion, fidl::MutatingVisitorTrait,
                                                 EncodingPosition, EnvelopeCheckpoint> {
 public:
  using Base = ::fidl::Visitor<WireFormatVersion, fidl::MutatingVisitorTrait, EncodingPosition,
                               EnvelopeCheckpoint>;
  using Status = typename Base::Status;
  using PointeeType = typename Base::PointeeType;
  using ObjectPointerPointer = typename Base::ObjectPointerPointer;
  using HandlePointer = typename Base::HandlePointer;
  using CountPointer = typename Base::CountPointer;
  using EnvelopeType = typename Base::EnvelopeType;
  using EnvelopePointer = typename Base::EnvelopePointer;
  using Position = EncodingPosition;

  FidlEncoder(EncodeArgs args)
      : current_iovec_uses_backing_buffer_(true),
        backing_buffer_(args.backing_buffer),
        backing_buffer_capacity_(args.backing_buffer_capacity),
        iovecs_(args.iovecs),
        iovecs_capacity_(args.iovecs_capacity),
        handles_(args.handles),
        handles_capacity_(args.handles_capacity),
        backing_buffer_offset_(args.inline_object_size),
        total_bytes_written_(args.inline_object_size),
        out_error_msg_(args.out_error_msg) {
    ZX_DEBUG_ASSERT(iovecs_capacity_ >= 1);
    ZX_DEBUG_ASSERT(backing_buffer_offset_ <= backing_buffer_capacity_);
    iovecs_[0] = {
        .buffer = args.backing_buffer,
        .capacity = args.inline_object_size,
    };
  }

  static constexpr bool kOnlyWalkResources = false;
  static constexpr bool kContinueAfterConstraintViolation = true;
  static constexpr bool kValidateEnvelopeInlineBit = false;

  Status VisitAbsentPointerInNonNullableCollection(ObjectPointerPointer object_ptr_ptr) {
    // Empty LLCPP vectors and strings typically have null data portions, which differs
    // from the wire format representation (0 length out-of-line object for empty vector
    // or string).
    // By marking the pointer as present, the wire format will have the correct
    // representation.
    *object_ptr_ptr = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
    return Status::kSuccess;
  }

  // Implementation of VisitPointer that points an iovec at a source object.
  Status VisitPointer_PointIovecAtObject(ObjectPointerPointer object_ptr_ptr, uint32_t inline_size,
                                         Position* out_position) {
    void* object_ptr = *object_ptr_ptr;
    // Add an iovec for the new object.
    iovec_idx_++;
    iovecs_[iovec_idx_] = {
        .buffer = object_ptr,
        .capacity = inline_size,
    };
    current_iovec_uses_backing_buffer_ = false;

    // Add an iovec for the next linearization target and add padding up to the out-of-line
    // alignment. For this padding allocate 8 bytes from the backing buffer and use only the
    // last |needed_padding| bytes, so that the next object being linearized will be aligned.
    if (inline_size % FIDL_ALIGNMENT != 0) {
      uint32_t needed_padding = FIDL_ALIGNMENT - inline_size % FIDL_ALIGNMENT;
      if (backing_buffer_offset_ + needed_padding > backing_buffer_capacity_) {
        SetError("Exceeded backing buffer size when adding padding");
        return Status::kMemoryError;
      }
      ZX_DEBUG_ASSERT(backing_buffer_offset_ % FIDL_ALIGNMENT == 0);
      *reinterpret_cast<uint64_t*>(&backing_buffer_[backing_buffer_offset_]) = 0;
      iovec_idx_++;
      iovecs_[iovec_idx_] = {
          .buffer = &backing_buffer_[backing_buffer_offset_] + inline_size % FIDL_ALIGNMENT,
          .capacity = needed_padding,
      };
      current_iovec_uses_backing_buffer_ = true;
      backing_buffer_offset_ += FIDL_ALIGNMENT;
    }

    *out_position = Position::Create(object_ptr, reinterpret_cast<uint8_t*>(object_ptr));

    // Rewrite pointer as "present" placeholder.
    *object_ptr_ptr = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
    return Status::kSuccess;
  }

  // Implementation of VisitPointer that linearizes to a buffer.
  Status VisitPointer_LinearizeToBuffer(ObjectPointerPointer object_ptr_ptr, uint32_t inline_size,
                                        Position* out_position) {
    void* object_ptr = *object_ptr_ptr;

    if (unlikely(!current_iovec_uses_backing_buffer_)) {
      iovec_idx_++;
      ZX_DEBUG_ASSERT_MSG(iovec_idx_ < iovecs_capacity_, "guaranteed by how iovecs are added");
      iovecs_[iovec_idx_] = {
          .buffer = &backing_buffer_[backing_buffer_offset_],
          .capacity = 0,
      };
      current_iovec_uses_backing_buffer_ = true;
    }

    uint64_t aligned_size = FidlAlign(inline_size);
    ZX_DEBUG_ASSERT(aligned_size >= inline_size);

    // Overflow check isn't needed because overflow of |total_bytes_written_| is checked first in
    // visit pointer.
    uint32_t new_backing_buffer_offset = backing_buffer_offset_ + aligned_size;
    uint32_t old_iovec_capacity = iovecs_[iovec_idx_].capacity;
    uint32_t new_iovec_capacity = old_iovec_capacity + aligned_size;

    if (unlikely(new_backing_buffer_offset > backing_buffer_capacity_)) {
      SetError("backing buffer size exceeded");
      return Status::kConstraintViolationError;
    }

    // Zero the last 8 bytes so that padding is zero after the memcpy.
    if (likely(inline_size != 0)) {
      *reinterpret_cast<uint64_t*>(__builtin_assume_aligned(
          &backing_buffer_[new_backing_buffer_offset - FIDL_ALIGNMENT], FIDL_ALIGNMENT)) = 0;
    }
    // Copy the pointee to the desired location in secondary storage
    memcpy(&backing_buffer_[backing_buffer_offset_], object_ptr, inline_size);

    // Instruct the walker to traverse the pointee afterwards.
    *out_position = Position::Create(object_ptr, backing_buffer_ + backing_buffer_offset_);

    backing_buffer_offset_ = new_backing_buffer_offset;
    iovecs_[iovec_idx_].capacity = new_iovec_capacity;

    // Rewrite pointer as "present" placeholder
    *object_ptr_ptr = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
    return Status::kSuccess;
  }

  Status VisitPointer(Position ptr_position, PointeeType pointee_type,
                      ObjectPointerPointer object_ptr_ptr, uint32_t inline_size,
                      FidlMemcpyCompatibility pointee_memcpy_compatibility,
                      Position* out_position) {
    if (unlikely(inline_size == 0)) {
      *object_ptr_ptr = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
      return Status::kSuccess;
    }

    uint64_t aligned_size = FidlAlign(inline_size);
    ZX_DEBUG_ASSERT(aligned_size >= inline_size);

    // |total_bytes_written_| is updated before calling the VisitPointer implementations as
    // |total_bytes_written_| is an upper bound for |iovecs_[iovec_idx_].capacity|, and
    // |backing_buffer_offset_| and doing this check first allows changes to the other values
    // to avoid overflow checks.
    if (unlikely(add_overflow(total_bytes_written_, aligned_size, &total_bytes_written_))) {
      SetError("overflowed while updating total bytes written");
      return Status::kMemoryError;
    }

    if (unlikely(pointee_memcpy_compatibility == kFidlMemcpyCompatibility_CanMemcpy)) {
      //  Validate we have a UTF-8 string.
      //  Note: strings are always memcpy compatible.
      //  TODO(fxbug.dev/52215): For strings, it would most likely be more efficient
      //  to validate and copy at the same time.
      if (unlikely(pointee_type == PointeeType::kString)) {
        auto validation_status =
            fidl_validate_string(reinterpret_cast<char*>(*object_ptr_ptr), inline_size);
        if (validation_status != ZX_OK) {
          SetError("encoder encountered invalid UTF8 string");
          return Status::kConstraintViolationError;
        }
      }

      // Note: In the worst case, two free iovecs are needed
      // (one for the object in question and one for any other objects that remain).
      if (likely(iovec_idx_ + 2 < iovecs_capacity_)) {
        return VisitPointer_PointIovecAtObject(object_ptr_ptr, inline_size, out_position);
      }
    }

    return VisitPointer_LinearizeToBuffer(object_ptr_ptr, inline_size, out_position);
  }

  Status VisitHandle(Position handle_position, HandlePointer dest_handle, zx_rights_t handle_rights,
                     zx_obj_type_t handle_subtype) {
    if (handle_idx_ == handles_capacity_) {
      SetError("message tried to encode too many handles");
      ThrowAwayHandle(dest_handle);
      return Status::kConstraintViolationError;
    }

    handles_[handle_idx_] = zx_handle_disposition_t{
        .operation = ZX_HANDLE_OP_MOVE,
        .handle = *dest_handle,
        .type = handle_subtype,
        .rights = handle_rights,
        .result = ZX_OK,
    };

    *dest_handle = FIDL_HANDLE_PRESENT;
    *handle_position.template GetFromSource<zx_handle_t>() = ZX_HANDLE_INVALID;
    handle_idx_++;
    return Status::kSuccess;
  }

  Status VisitVectorOrStringCount(CountPointer ptr) { return Status::kSuccess; }

  template <typename MaskType>
  Status VisitInternalPadding(Position padding_position, MaskType mask) {
    MaskType* ptr = padding_position.template Get<MaskType>();
    *ptr &= static_cast<MaskType>(~mask);
    return Status::kSuccess;
  }

  EnvelopeCheckpoint EnterEnvelope() {
    return {
        .num_bytes = total_bytes_written_,
        .num_handles = handle_idx_,
    };
  }

  Status LeaveEnvelopeImpl(fidl_envelope_t in_envelope, fidl_envelope_t* out_envelope,
                           EnvelopeCheckpoint prev_checkpoint) {
    uint32_t num_bytes = total_bytes_written_ - prev_checkpoint.num_bytes;
    uint32_t num_handles = handle_idx_ - prev_checkpoint.num_handles;
    // Write the num_bytes/num_handles.
    out_envelope->num_bytes = num_bytes;
    out_envelope->num_handles = num_handles;
    return Status::kSuccess;
  }
  Status LeaveEnvelopeImpl(fidl_envelope_v2_t in_envelope, fidl_envelope_v2_t* out_envelope,
                           EnvelopeCheckpoint prev_checkpoint) {
    uint32_t num_bytes = total_bytes_written_ - prev_checkpoint.num_bytes;
    uint32_t num_handles = handle_idx_ - prev_checkpoint.num_handles;
    // Write the num_bytes/num_handles.
    out_envelope->num_bytes = num_bytes;
    out_envelope->num_handles = num_handles;
    out_envelope->flags = 0;
    return Status::kSuccess;
  }
  Status LeaveEnvelope(EnvelopeType in_envelope, EnvelopePointer out_envelope,
                       EnvelopeCheckpoint prev_checkpoint) {
    return LeaveEnvelopeImpl(in_envelope, out_envelope, prev_checkpoint);
  }

  Status LeaveInlinedEnvelopeImpl(fidl_envelope_t in_envelope, fidl_envelope_t* out_envelope,
                                  EnvelopeCheckpoint prev_checkpoint) {
    ZX_PANIC("Not implemented for v1");
  }
  Status LeaveInlinedEnvelopeImpl(fidl_envelope_v2_t in_envelope, fidl_envelope_v2_t* out_envelope,
                                  EnvelopeCheckpoint prev_checkpoint) {
    out_envelope->num_handles = handle_idx_ - prev_checkpoint.num_handles;
    out_envelope->flags = FIDL_ENVELOPE_FLAGS_INLINING_MASK;
    if (out_envelope->num_handles != 0) {
      // FIDL_HANDLE_PRESENT
      memset(out_envelope->inline_value, 0xff, sizeof(out_envelope->inline_value));
    }
    return Status::kSuccess;
  }
  Status LeaveInlinedEnvelope(EnvelopeType in_envelope, EnvelopePointer out_envelope,
                              EnvelopeCheckpoint prev_checkpoint) {
    return LeaveInlinedEnvelopeImpl(in_envelope, out_envelope, prev_checkpoint);
  }

  // Error when attempting to encode an unknown envelope.
  // This behavior is LLCPP specific, and so assumes that the FidlEncoder is only
  // used in LLCPP.
  Status VisitUnknownEnvelope(EnvelopeType envelope_copy, EnvelopePointer envelope_ptr,
                              FidlIsResource is_resource) {
    SetError("Cannot encode unknown union or table");
    return Status::kConstraintViolationError;
  }

  void OnError(const char* error) { SetError(error); }

  zx_status_t status() const { return status_; }

  uint32_t num_out_handles() const { return handle_idx_; }
  uint32_t num_out_iovecs() const { return iovec_idx_ + 1; }

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

  bool current_iovec_uses_backing_buffer_ = false;
  uint8_t* const backing_buffer_ = nullptr;
  const uint32_t backing_buffer_capacity_ = 0;
  zx_channel_iovec_t* const iovecs_ = nullptr;
  const uint32_t iovecs_capacity_ = 0;
  zx_handle_disposition_t* handles_ = nullptr;
  const uint32_t handles_capacity_ = 0;

  // backing_buffer_offset_ is always 8-byte aligned.
  uint32_t backing_buffer_offset_ = 0;
  uint32_t iovec_idx_ = 0;
  uint32_t handle_idx_ = 0;
  uint32_t total_bytes_written_ = 0;

  zx_status_t status_ = ZX_OK;
  const char** const out_error_msg_ = nullptr;
};
}  // namespace

namespace fidl {
namespace internal {

#define USER_ASSERT(condition, message) \
  if (unlikely(!(condition))) {         \
    *out_error_msg = message;           \
    return ZX_ERR_INVALID_ARGS;         \
  }

template <FidlWireFormatVersion WireFormatVersion>
zx_status_t EncodeIovecEtc(const fidl_type_t* type, void* value, zx_channel_iovec_t* iovecs,
                           uint32_t num_iovecs, zx_handle_disposition_t* handle_dispositions,
                           uint32_t num_handle_dispositions, uint8_t* backing_buffer,
                           uint32_t num_backing_buffer, uint32_t* out_actual_iovec,
                           uint32_t* out_actual_handles, const char** out_error_msg) {
  // Use debug asserts for preconditions that are not user dependent to avoid the runtime cost.
  ZX_DEBUG_ASSERT(type != nullptr);
  ZX_DEBUG_ASSERT(iovecs != nullptr);
  ZX_DEBUG_ASSERT(out_actual_iovec != nullptr);
  ZX_DEBUG_ASSERT(out_actual_handles != nullptr);
  ZX_DEBUG_ASSERT(out_error_msg != nullptr);
  ZX_DEBUG_ASSERT(num_iovecs > 0);

  // Return errors for user-input dependent errors.
  USER_ASSERT(value != nullptr, "Cannot encode null value");
  USER_ASSERT(backing_buffer != nullptr, "Cannot encode to null byte array");
  USER_ASSERT(FidlIsAligned(reinterpret_cast<uint8_t*>(value)),
              "value must be aligned to FIDL_ALIGNMENT");
  USER_ASSERT(FidlIsAligned(reinterpret_cast<uint8_t*>(backing_buffer)),
              "backing_buffer must be aligned to FIDL_ALIGNMENT");
  USER_ASSERT(num_backing_buffer % FIDL_ALIGNMENT == 0,
              "num_backing_buffer must be aligned to FIDL_ALIGNMENT");
  USER_ASSERT(handle_dispositions != nullptr || num_handle_dispositions == 0,
              "Cannot provide non-zero handle count and null handle pointer");

  zx_status_t status;
  uint32_t primary_size;
  uint32_t next_out_of_line;
  if (unlikely((status = fidl::PrimaryObjectSize<WireFormatVersion>(
                    type, num_backing_buffer, &primary_size, &next_out_of_line, out_error_msg)) !=
               ZX_OK)) {
    return status;
  }

  // Zero the last 8 bytes so padding will be zero after memcpy.
  *reinterpret_cast<uint64_t*>(__builtin_assume_aligned(
      &backing_buffer[next_out_of_line - FIDL_ALIGNMENT], FIDL_ALIGNMENT)) = 0;

  // Copy the primary object
  memcpy(backing_buffer, value, primary_size);

  EncodeArgs args = {
      .backing_buffer = static_cast<uint8_t*>(backing_buffer),
      .backing_buffer_capacity = num_backing_buffer,
      .iovecs = iovecs,
      .iovecs_capacity = num_iovecs,
      .handles = handle_dispositions,
      .handles_capacity = num_handle_dispositions,
      .inline_object_size = next_out_of_line,
      .out_error_msg = out_error_msg,
  };
  if (handle_dispositions != nullptr) {
    args.handles = handle_dispositions;
  }
  FidlEncoder<WireFormatVersion> encoder(args);
  ::fidl::Walk<WireFormatVersion>(encoder, type, {.source_object = value, .dest = backing_buffer});
  if (unlikely(encoder.status() != ZX_OK)) {
    *out_actual_handles = 0;
    FidlHandleDispositionCloseMany(handle_dispositions, encoder.num_out_handles());
    return ZX_ERR_INVALID_ARGS;
  }

  *out_actual_iovec = encoder.num_out_iovecs();
  *out_actual_handles = encoder.num_out_handles();
  return ZX_OK;
}

template zx_status_t EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V1>(
    const fidl_type_t* type, void* value, zx_channel_iovec_t* iovecs, uint32_t num_iovecs,
    zx_handle_disposition_t* handle_dispositions, uint32_t num_handle_dispositions,
    uint8_t* backing_buffer, uint32_t num_backing_buffer, uint32_t* out_actual_iovec,
    uint32_t* out_actual_handles, const char** out_error_msg);
template zx_status_t EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
    const fidl_type_t* type, void* value, zx_channel_iovec_t* iovecs, uint32_t num_iovecs,
    zx_handle_disposition_t* handle_dispositions, uint32_t num_handle_dispositions,
    uint8_t* backing_buffer, uint32_t num_backing_buffer, uint32_t* out_actual_iovec,
    uint32_t* out_actual_handles, const char** out_error_msg);

}  // namespace internal
}  // namespace fidl
