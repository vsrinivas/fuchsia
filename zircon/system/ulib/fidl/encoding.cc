// Copyright 2017 The Fuchsia Authors. All rights reserved.
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
  // |dest| is an address in the destination buffer.
  uint8_t* dest;
  __ALWAYS_INLINE static EncodingPosition Create(void* source_object, uint8_t* dest) {
    return {
        .dest = dest,
    };
  }
  __ALWAYS_INLINE EncodingPosition operator+(uint32_t size) const {
    return {
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
  template <typename T>
  __ALWAYS_INLINE constexpr T* GetFromSource() const {
    ZX_PANIC("GetFromSource should not be used when encoding without linearizing");
  }
};

struct EnvelopeCheckpoint {
  uint32_t num_bytes;
  uint32_t num_handles;
};

struct BufferEncodeArgs {
  uint8_t* bytes;
  uint32_t num_bytes;
  cpp17::variant<cpp17::monostate, zx_handle_t*, zx_handle_disposition_t*> handles;
  uint32_t num_handles;
  uint32_t next_out_of_line;
  const char** out_error_msg;
};

class FidlEncoder final
    : public ::fidl::Visitor<FIDL_WIRE_FORMAT_VERSION_V1, fidl::MutatingVisitorTrait,
                             EncodingPosition, EnvelopeCheckpoint> {
 public:
  using Base = ::fidl::Visitor<FIDL_WIRE_FORMAT_VERSION_V1, fidl::MutatingVisitorTrait,
                               EncodingPosition, EnvelopeCheckpoint>;
  using Status = typename Base::Status;
  using PointeeType = typename Base::PointeeType;
  using ObjectPointerPointer = typename Base::ObjectPointerPointer;
  using HandlePointer = typename Base::HandlePointer;
  using CountPointer = typename Base::CountPointer;
  using EnvelopePointer = typename Base::EnvelopePointer;
  using Position = EncodingPosition;

  FidlEncoder(BufferEncodeArgs args)
      : bytes_(args.bytes),
        num_bytes_(args.num_bytes),
        handles_(args.handles),
        num_handles_(args.num_handles),
        next_out_of_line_(args.next_out_of_line),
        out_error_msg_(args.out_error_msg) {}

  static constexpr bool kOnlyWalkResources = false;
  static constexpr bool kContinueAfterConstraintViolation = true;

  Status VisitAbsentPointerInNonNullableCollection(ObjectPointerPointer object_ptr_ptr) {
    SetError("absent pointer disallowed in non-nullable collection");
    return Status::kConstraintViolationError;
  }

  Status VisitPointerBuffer(Position ptr_position, PointeeType pointee_type, void* object_ptr,
                            uint32_t new_offset, uint32_t inline_size, Position* out_position) {
    if (unlikely(new_offset > num_bytes_)) {
      SetError("pointed offset exceeds buffer size");
      return Status::kConstraintViolationError;
    }
    if (unlikely(object_ptr != &bytes_[next_out_of_line_])) {
      SetError("noncontiguous out of line storage during encode");
      return Status::kMemoryError;
    } else {
      // Zero padding between out of line storage.
      memset(&bytes_[next_out_of_line_] + inline_size, 0,
             (new_offset - next_out_of_line_) - inline_size);
    }

    // Instruct the walker to traverse the pointee afterwards.
    *out_position = Position::Create(object_ptr, bytes_ + next_out_of_line_);
    return Status::kSuccess;
  }

  Status VisitPointer(Position ptr_position, PointeeType pointee_type,
                      ObjectPointerPointer object_ptr_ptr, uint32_t inline_size,
                      FidlMemcpyCompatibility pointee_memcpy_compatibility,
                      Position* out_position) {
    // For pointers in types other than vectors and strings, the LSB is reserved to mark ownership
    // and may be set to 1 if the object is heap allocated. However, the original pointer has this
    // bit cleared. For vectors and strings, any value is accepted.
    void* object_ptr = *object_ptr_ptr;
    uint32_t new_offset;
    if (unlikely(!FidlAddOutOfLine(next_out_of_line_, inline_size, &new_offset))) {
      SetError("overflow updating out-of-line offset");
      return Status::kMemoryError;
    }

    // Validate that we have a UTF8 string.
    // TODO(fxbug.dev/52215): For strings, it would most likely be more efficient
    // to validate and copy at the same time.
    if (unlikely(pointee_type == PointeeType::kString)) {
      auto validation_status =
          fidl_validate_string(reinterpret_cast<char*>(object_ptr), inline_size);
      if (validation_status != ZX_OK) {
        SetError("encoder encountered invalid UTF8 string");
        return Status::kConstraintViolationError;
      }
    }

    Status status = VisitPointerBuffer(ptr_position, pointee_type, object_ptr, new_offset,
                                       inline_size, out_position);
    if (status != Status::kSuccess) {
      return status;
    }

    next_out_of_line_ = new_offset;

    // Rewrite pointer as "present" placeholder
    return SetPointerPresent(object_ptr_ptr, object_ptr);
  }

  Status VisitHandle(Position handle_position, HandlePointer dest_handle, zx_rights_t handle_rights,
                     zx_obj_type_t handle_subtype) {
    if (handle_idx_ == num_handles_) {
      SetError("message tried to encode too many handles");
      ThrowAwayHandle(dest_handle);
      return Status::kConstraintViolationError;
    }

    if (has_handles()) {
      handles()[handle_idx_] = *dest_handle;
    } else if (likely(has_handle_dispositions())) {
      handle_dispositions()[handle_idx_] = zx_handle_disposition_t{
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = *dest_handle,
          .type = handle_subtype,
          .rights = handle_rights,
          .result = ZX_OK,
      };
    } else {
      SetError("did not provide place to store handles");
      ThrowAwayHandle(dest_handle);
      return Status::kConstraintViolationError;
    }

    *dest_handle = FIDL_HANDLE_PRESENT;
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
        .num_bytes = next_out_of_line_,
        .num_handles = handle_idx_,
    };
  }

  Status LeaveEnvelope(EnvelopePointer envelope, EnvelopeCheckpoint prev_checkpoint) {
    uint32_t num_bytes = next_out_of_line_ - prev_checkpoint.num_bytes;
    uint32_t num_handles = handle_idx_ - prev_checkpoint.num_handles;
    // Validate the claimed num_bytes/num_handles.
    if (unlikely(envelope->num_bytes != num_bytes)) {
      SetError("Envelope num_bytes was mis-sized");
      return Status::kConstraintViolationError;
    }
    if (unlikely(envelope->num_handles != num_handles)) {
      SetError("Envelope num_handles was mis-sized");
      return Status::kConstraintViolationError;
    }
    return Status::kSuccess;
  }

  // Error when attempting to encode an unknown envelope.
  // Unknown envelopes are not supported in C, which is the only user of FidlEncoder.
  Status VisitUnknownEnvelope(EnvelopePointer envelope, FidlIsResource is_resource) {
    SetError("Cannot encode unknown union or table");
    return Status::kConstraintViolationError;
  }

  void OnError(const char* error) { SetError(error); }

  zx_status_t status() const { return status_; }

  uint32_t num_out_handles() const { return handle_idx_; }
  uint32_t num_out_bytes() const { return next_out_of_line_; }

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

  bool has_handles() const { return cpp17::holds_alternative<zx_handle_t*>(handles_); }
  bool has_handle_dispositions() const {
    return cpp17::holds_alternative<zx_handle_disposition_t*>(handles_);
  }
  zx_handle_t* handles() const {
    ZX_DEBUG_ASSERT(cpp17::get<zx_handle_t*>(handles_) != nullptr);
    return cpp17::get<zx_handle_t*>(handles_);
  }
  zx_handle_disposition_t* handle_dispositions() const {
    ZX_DEBUG_ASSERT(cpp17::get<zx_handle_disposition_t*>(handles_) != nullptr);
    return cpp17::get<zx_handle_disposition_t*>(handles_);
  }

  Status SetPointerPresent(ObjectPointerPointer object_ptr_ptr, void* object_ptr) {
    *object_ptr_ptr = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
    return Status::kSuccess;
  }

  // Message state initialized in the constructor.
  uint8_t* const bytes_ = nullptr;
  const uint32_t num_bytes_ = 0;
  cpp17::variant<cpp17::monostate, zx_handle_t*, zx_handle_disposition_t*> handles_;
  const uint32_t num_handles_;
  uint32_t next_out_of_line_;
  const char** const out_error_msg_;

  // Encoder state
  zx_status_t status_ = ZX_OK;
  uint32_t handle_idx_ = 0;
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
  if (unlikely(type == nullptr)) {
    set_error("fidl type cannot be null");
    return ZX_ERR_INVALID_ARGS;
  }
  if (unlikely(bytes == nullptr)) {
    set_error("Cannot encode null bytes");
    return ZX_ERR_INVALID_ARGS;
  }
  if (unlikely(!FidlIsAligned(reinterpret_cast<uint8_t*>(bytes)))) {
    set_error("Bytes must be aligned to FIDL_ALIGNMENT");
    return ZX_ERR_INVALID_ARGS;
  }
  if (unlikely(num_bytes % FIDL_ALIGNMENT != 0)) {
    set_error("num_bytes must be aligned to FIDL_ALIGNMENT");
    return ZX_ERR_INVALID_ARGS;
  }

  // Zero region between primary object and next out of line object.
  zx_status_t status;
  uint32_t primary_size;
  uint32_t next_out_of_line;
  if (unlikely((status = fidl::PrimaryObjectSize<FIDL_WIRE_FORMAT_VERSION_V1>(
                    type, num_bytes, &primary_size, &next_out_of_line, out_error_msg)) != ZX_OK)) {
    return status;
  }
  memset(reinterpret_cast<uint8_t*>(bytes) + primary_size, 0, next_out_of_line - primary_size);

  BufferEncodeArgs args = {.bytes = static_cast<uint8_t*>(bytes),
                           .num_bytes = num_bytes,
                           .num_handles = max_handles,
                           .next_out_of_line = next_out_of_line,
                           .out_error_msg = out_error_msg};
  if (handles != nullptr) {
    args.handles = handles;
  }
  FidlEncoder encoder(args);
  fidl::Walk<FIDL_WIRE_FORMAT_VERSION_V1>(encoder, type,
                                          {.dest = reinterpret_cast<uint8_t*>(bytes)});

  auto drop_all_handles = [&]() {
    if (out_actual_handles) {
      *out_actual_handles = 0;
    }
    close_handles(handles, encoder.num_out_handles());
  };

  if (likely(encoder.status() == ZX_OK)) {
    if (unlikely(encoder.num_out_bytes() != num_bytes)) {
      set_error("message did not encode all provided bytes");
      drop_all_handles();
      return ZX_ERR_INVALID_ARGS;
    }
    if (unlikely(out_actual_handles == nullptr)) {
      set_error("Cannot encode with null out_actual_handles");
      drop_all_handles();
      return ZX_ERR_INVALID_ARGS;
    }
    *out_actual_handles = encoder.num_out_handles();
  } else {
    drop_all_handles();
    return ZX_ERR_INVALID_ARGS;
  }

  if (unlikely(handles == nullptr && max_handles != 0)) {
    set_error("Cannot provide non-zero handle count and null handle pointer");
    // When |handles| is nullptr, handles are closed as part of traversal.
    return ZX_ERR_INVALID_ARGS;
  }

  return encoder.status();
}

void close_handles_op(const zx_handle_t* handles, uint32_t max_idx) {
  // Return value intentionally ignored. This is best-effort cleanup.
  FidlHandleCloseMany(handles, max_idx);
}

void close_handle_dispositions_op(const zx_handle_disposition_t* handle_dispositions,
                                  uint32_t max_idx) {
  // Return value intentionally ignored. This is best-effort cleanup.
  FidlHandleDispositionCloseMany(handle_dispositions, max_idx);
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

zx_status_t fidl_encode_msg(const fidl_type_t* type, fidl_outgoing_msg_byte_t* msg,
                            uint32_t* out_actual_handles, const char** out_error_msg) {
  return fidl_encode_etc(type, msg->bytes, msg->num_bytes, msg->handles, msg->num_handles,
                         out_actual_handles, out_error_msg);
}
