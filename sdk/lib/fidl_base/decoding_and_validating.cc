// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/visitor.h>
#include <lib/fidl/walker.h>
#include <lib/stdcompat/variant.h>
#include <lib/utf-utils/utf-utils.h>
#include <stdalign.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/fidl.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

// TODO(kulakowski) Design zx_status_t error values.

namespace {

template <typename Byte>
struct DecodingPosition {
  Byte* addr;
  DecodingPosition operator+(uint32_t size) const { return DecodingPosition{addr + size}; }
  DecodingPosition& operator+=(uint32_t size) {
    addr += size;
    return *this;
  }
  template <typename T, typename U = std::conditional_t<std::is_const<Byte>::value, const T, T>>
  constexpr U* Get() const {
    return reinterpret_cast<U*>(addr);
  }
};

struct EnvelopeCheckpoint {
  uint32_t num_bytes;
  uint32_t num_handles;
};

constexpr zx_rights_t subtract_rights(zx_rights_t minuend, zx_rights_t subtrahend) {
  return minuend & ~subtrahend;
}
static_assert(subtract_rights(0b011, 0b101) == 0b010, "ensure rights subtraction works correctly");

enum class Mode { Decode, Validate };

template <Mode mode, typename T, typename U>
void AssignInDecode(T* ptr, U value) {
  static_assert(mode == Mode::Decode, "only assign if decode");
  *ptr = value;
}

template <Mode mode, typename T, typename U>
void AssignInDecode(const T* ptr, U value) {
  static_assert(mode == Mode::Validate, "don't assign if validate");
  // nothing in validate mode
}

template <Mode mode>
zx_status_t DecodeProcessHandle(fidl_handle_t* handle, zx_obj_type_t obj_type, zx_rights_t rights,
                                uint32_t metadata_index, const void* metadata_array,
                                const char** error) {
  static_assert(mode == Mode::Decode, "process handles during decode");
  fidl_channel_handle_metadata_t v =
      reinterpret_cast<const fidl_channel_handle_metadata_t*>(metadata_array)[metadata_index];
  return FidlEnsureHandleRights(handle, v.obj_type, v.rights, obj_type, rights, error);
}

template <Mode mode>
zx_status_t DecodeProcessHandle(const fidl_handle_t* handle, zx_obj_type_t obj_type,
                                zx_rights_t rights, uint32_t metadata_index,
                                const void* metadata_array, const char** error) {
  static_assert(mode == Mode::Validate, "never used during validate");
  __builtin_unreachable();
}

void ConvertEnvelopeToDecodedRepresentation(const void* bytes_base_ptr,
                                            fidl_envelope_v2_t envelope_copy,
                                            const fidl_envelope_v2_t* envelope_ptr) {
  // No conversion is needed for v2 validate.
}
void ConvertEnvelopeToDecodedRepresentation(const void* bytes_base_ptr,
                                            fidl_envelope_v2_t envelope_copy,
                                            fidl_envelope_v2_t* envelope_ptr) {
  if ((envelope_copy.flags & FIDL_ENVELOPE_FLAGS_INLINING_MASK) != 0) {
    fidl_envelope_v2_unknown_data_t unknown_data_envelope = {
        .num_handles = envelope_copy.num_handles,
        .flags = envelope_copy.flags,
    };
    memcpy(unknown_data_envelope.inline_value, envelope_ptr->inline_value,
           sizeof(envelope_ptr->inline_value));
    memcpy(envelope_ptr, &unknown_data_envelope, sizeof(unknown_data_envelope));
    return;
  }

  uintptr_t data_ptr = *reinterpret_cast<uintptr_t*>(envelope_ptr);
  uintptr_t base_ptr = reinterpret_cast<uintptr_t>(bytes_base_ptr);
  uintptr_t offset = data_ptr - base_ptr;
  ZX_ASSERT(offset <= std::numeric_limits<uint16_t>::max());
  ZX_ASSERT(envelope_copy.num_bytes <= std::numeric_limits<uint16_t>::max());
  fidl_envelope_v2_unknown_data_t unknown_data_envelope = {
      .out_of_line =
          {
              .num_bytes = static_cast<uint16_t>(envelope_copy.num_bytes),
              .offset = static_cast<uint16_t>(offset),
          },
      .num_handles = envelope_copy.num_handles,
      .flags = envelope_copy.flags,
  };
  memcpy(envelope_ptr, &unknown_data_envelope, sizeof(unknown_data_envelope));
}

template <FidlWireFormatVersion WireFormatVersion, typename Byte>
using BaseVisitor =
    fidl::Visitor<WireFormatVersion,
                  std::conditional_t<std::is_const<Byte>::value, fidl::NonMutatingVisitorTrait,
                                     fidl::MutatingVisitorTrait>,
                  DecodingPosition<Byte>, EnvelopeCheckpoint>;

template <Mode mode, FidlWireFormatVersion WireFormatVersion, typename Byte>
class FidlDecoder final : public BaseVisitor<WireFormatVersion, Byte> {
 public:
  FidlDecoder(Byte* bytes, uint32_t num_bytes, const fidl_handle_t* handles,
              const void* handle_metadata, uint32_t num_handles, uint32_t next_out_of_line,
              const char** out_error_msg, bool hlcpp_mode)
      : bytes_(bytes),
        num_bytes_(num_bytes),
        handles_(handles),
        handle_metadata_(handle_metadata),
        num_handles_(num_handles),
        next_out_of_line_(next_out_of_line),
        out_error_msg_(out_error_msg),
        hlcpp_mode_(hlcpp_mode) {}
  using Position = typename BaseVisitor<WireFormatVersion, Byte>::Position;
  using Status = typename BaseVisitor<WireFormatVersion, Byte>::Status;
  using PointeeType = typename BaseVisitor<WireFormatVersion, Byte>::PointeeType;
  using ObjectPointerPointer = typename BaseVisitor<WireFormatVersion, Byte>::ObjectPointerPointer;
  using HandlePointer = typename BaseVisitor<WireFormatVersion, Byte>::HandlePointer;
  using CountPointer = typename BaseVisitor<WireFormatVersion, Byte>::CountPointer;
  using EnvelopeType = typename BaseVisitor<WireFormatVersion, Byte>::EnvelopeType;
  using EnvelopePointer = typename BaseVisitor<WireFormatVersion, Byte>::EnvelopePointer;

  static constexpr bool kOnlyWalkResources = false;
  static constexpr bool kContinueAfterConstraintViolation = false;
  static constexpr bool kValidateEnvelopeInlineBit = true;

  Status VisitAbsentPointerInNonNullableCollection(ObjectPointerPointer object_ptr_ptr) {
    SetError("absent pointer disallowed in non-nullable collection");
    return Status::kConstraintViolationError;
  }

  Status VisitPointer(Position ptr_position, PointeeType pointee_type,
                      ObjectPointerPointer object_ptr_ptr, uint32_t inline_size,
                      FidlMemcpyCompatibility pointee_memcpy_compatibility,
                      Position* out_position) {
    if (unlikely(pointee_type != PointeeType::kEnvelope &&
                 reinterpret_cast<uintptr_t>(*object_ptr_ptr) != FIDL_ALLOC_PRESENT)) {
      SetError("invalid presence marker");
      return Status::kMemoryError;
    }
    uint32_t new_offset;
    if (unlikely(!FidlAddOutOfLine(next_out_of_line_, inline_size, &new_offset))) {
      SetError("overflow updating out-of-line offset");
      return Status::kMemoryError;
    }
    if (unlikely(new_offset > num_bytes_)) {
      SetError("message tried to access more than provided number of bytes");
      return Status::kMemoryError;
    }
    {
      if (inline_size % FIDL_ALIGNMENT != 0) {
        // Validate the last 8-byte block.
        const uint64_t* block_end = reinterpret_cast<const uint64_t*>(&bytes_[new_offset]) - 1;
        uint64_t padding_len = new_offset - next_out_of_line_ - inline_size;
        uint64_t padding_mask = ~0ull << (64 - 8 * padding_len);
        auto status = ValidatePadding(block_end, padding_mask);
        if (status != Status::kSuccess) {
          return status;
        }
      }
    }
    if (unlikely(pointee_type == PointeeType::kString)) {
      bool valid = utfutils_is_valid_utf8(reinterpret_cast<const char*>(&bytes_[next_out_of_line_]),
                                          inline_size);
      if (!valid) {
        SetError("encountered invalid UTF8 string");
        return Status::kConstraintViolationError;
      }
    }
    *out_position = Position{bytes_ + next_out_of_line_};
    AssignInDecode<mode>(
        object_ptr_ptr,
        reinterpret_cast<std::remove_pointer_t<ObjectPointerPointer>>(&bytes_[next_out_of_line_]));

    next_out_of_line_ = new_offset;
    return Status::kSuccess;
  }

  Status VisitHandle(Position handle_position, HandlePointer handle,
                     zx_rights_t required_handle_rights, zx_obj_type_t required_handle_subtype) {
    if (unlikely(*handle != FIDL_HANDLE_PRESENT)) {
      SetError("message tried to decode a garbage handle");
      return Status::kConstraintViolationError;
    }
    if (unlikely(handle_idx_ == num_handles_)) {
      SetError("message decoded too many handles");
      return Status::kConstraintViolationError;
    }

    if (mode == Mode::Validate) {
      handle_idx_++;
      return Status::kSuccess;
    }

    if (unlikely(handles_[handle_idx_] == ZX_HANDLE_INVALID)) {
      SetError("invalid handle detected in handle table");
      return Status::kConstraintViolationError;
    }
    if (mode == Mode::Decode) {
      AssignInDecode<mode>(handle, handles_[handle_idx_]);

      const char* error;
      zx_status_t status =
          DecodeProcessHandle<mode>(handle, required_handle_subtype, required_handle_rights,
                                    handle_idx_, handle_metadata_, &error);
      if (status != ZX_OK) {
        SetError(error);
        return Status::kConstraintViolationError;
      }
    }
    handle_idx_++;
    return Status::kSuccess;
  }

  Status VisitVectorOrStringCount(CountPointer ptr) { return Status::kSuccess; }

  template <typename MaskType>
  Status VisitInternalPadding(Position padding_position, MaskType mask) {
    return ValidatePadding(padding_position.template Get<const MaskType>(), mask);
  }

  EnvelopeCheckpoint EnterEnvelope() {
    return {
        .num_bytes = next_out_of_line_,
        .num_handles = handle_idx_,
    };
  }

  Status LeaveEnvelope(EnvelopeType in_envelope, EnvelopePointer out_envelope,
                       EnvelopeCheckpoint prev_checkpoint) {
    // Now that the envelope has been consumed, check the correctness of the envelope header.
    uint32_t num_bytes = next_out_of_line_ - prev_checkpoint.num_bytes;
    uint32_t num_handles = handle_idx_ - prev_checkpoint.num_handles;
    if (unlikely(in_envelope.num_bytes != num_bytes)) {
      SetError("Envelope num_bytes was mis-sized");
      return Status::kConstraintViolationError;
    }
    if (unlikely(in_envelope.num_handles != num_handles)) {
      SetError("Envelope num_handles was mis-sized");
      return Status::kConstraintViolationError;
    }
    return Status::kSuccess;
  }

  Status LeaveInlinedEnvelope(EnvelopeType in_envelope, EnvelopePointer out_envelope,
                              EnvelopeCheckpoint prev_checkpoint) {
    // Now that the envelope has been consumed, check the correctness of the envelope header.
    uint32_t num_handles = handle_idx_ - prev_checkpoint.num_handles;
    if (unlikely(in_envelope.num_handles != num_handles)) {
      SetError("Envelope num_handles was mis-sized");
      return Status::kConstraintViolationError;
    }
    return Status::kSuccess;
  }

  Status VisitUnknownEnvelope(EnvelopeType envelope_copy, EnvelopePointer envelope_ptr,
                              FidlIsResource is_resource) {
    if (mode == Mode::Validate) {
      handle_idx_ += envelope_copy.num_handles;
      return Status::kSuccess;
    }

    if (hlcpp_mode_) {
      ConvertEnvelopeToDecodedRepresentation(bytes_, envelope_copy, envelope_ptr);
    }

    // If we do not have the coding table for this payload,
    // treat it as unknown and close its contained handles
    if (unlikely(envelope_copy.num_handles > 0)) {
      uint32_t total_unknown_handles;
      if (add_overflow(unknown_handle_idx_, envelope_copy.num_handles, &total_unknown_handles)) {
        SetError("number of unknown handles overflows");
        return Status::kConstraintViolationError;
      }
      if (total_unknown_handles > ZX_CHANNEL_MAX_MSG_HANDLES) {
        SetError("number of unknown handles exceeds unknown handle array size");
        return Status::kConstraintViolationError;
      }
      uint32_t end_incoming_handle;
      if (add_overflow(handle_idx_, envelope_copy.num_handles, &end_incoming_handle)) {
        SetError("number of incoming handles overflows");
        return Status::kConstraintViolationError;
      }
      if (end_incoming_handle > num_handles_) {
        SetError("number of incoming handles exceeds incoming handle array size");
        return Status::kConstraintViolationError;
      }
      // If hlcpp_mode_ is true, leave the unknown handles intact
      // for something else to process (e.g. HLCPP Decode)
      if (hlcpp_mode_ && is_resource == kFidlIsResource_Resource) {
        handle_idx_ += envelope_copy.num_handles;
        return Status::kSuccess;
      }
      memcpy(&unknown_handles_[unknown_handle_idx_], &handles_[handle_idx_],
             envelope_copy.num_handles * sizeof(fidl_handle_t));
      handle_idx_ = end_incoming_handle;
      unknown_handle_idx_ = total_unknown_handles;
    }
    return Status::kSuccess;
  }

  void OnError(const char* error) { SetError(error); }

  zx_status_t status() const { return status_; }

  bool DidConsumeAllBytes() const { return next_out_of_line_ == num_bytes_; }

  bool DidConsumeAllHandles() const { return handle_idx_ == num_handles_; }

  uint32_t unknown_handle_idx() const { return unknown_handle_idx_; }

  const fidl_handle_t* unknown_handles() const { return unknown_handles_; }

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

  template <typename MaskType>
  Status ValidatePadding(const MaskType* padding_ptr, MaskType mask) {
    if ((*padding_ptr & mask) != 0) {
      SetError("non-zero padding bytes detected");
      return Status::kConstraintViolationError;
    }
    return Status::kSuccess;
  }

  // Message state passed in to the constructor.
  Byte* const bytes_;
  const uint32_t num_bytes_;
  const fidl_handle_t* handles_;
  const void* handle_metadata_;
  const uint32_t num_handles_;
  uint32_t next_out_of_line_;
  const char** const out_error_msg_;
  // HLCPP first uses FidlDecoder to do an in-place decode, then extracts data
  // out into domain objects. Since HLCPP stores unknown handles
  // (and LLCPP does not), this field allows HLCPP to use the decoder while
  // keeping unknown handles in flexible resource unions intact.
  bool hlcpp_mode_;

  // Decoder state
  zx_status_t status_ = ZX_OK;
  uint32_t handle_idx_ = 0;
  uint32_t unknown_handle_idx_ = 0;
  fidl_handle_t unknown_handles_[ZX_CHANNEL_MAX_MSG_HANDLES];
};

}  // namespace

namespace {
template <FidlWireFormatVersion WireFormatVersion>
zx_status_t fidl_decode_impl(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                             const fidl_handle_t* handles, const void* handle_metadata,
                             uint32_t num_handles, const char** out_error_msg, bool hlcpp_mode) {
  auto drop_all_handles = [&]() { FidlHandleCloseMany(handles, num_handles); };
  auto set_error = [&out_error_msg](const char* msg) {
    if (out_error_msg)
      *out_error_msg = msg;
  };
  if (unlikely(type == nullptr)) {
    set_error("fidl type cannot be null");
    return ZX_ERR_INVALID_ARGS;
  }
  if (unlikely(handles == nullptr && num_handles != 0)) {
    set_error("Cannot provide non-zero handle count and null handle pointer");
    return ZX_ERR_INVALID_ARGS;
  }
  if (unlikely(bytes == nullptr)) {
    set_error("Cannot decode null bytes");
    drop_all_handles();
    return ZX_ERR_INVALID_ARGS;
  }
  if (unlikely(!FidlIsAligned(reinterpret_cast<uint8_t*>(bytes)))) {
    set_error("Bytes must be aligned to FIDL_ALIGNMENT");
    drop_all_handles();
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status;
  uint32_t primary_size;
  uint32_t next_out_of_line;
  if (unlikely((status = fidl::PrimaryObjectSize<WireFormatVersion>(
                    type, num_bytes, &primary_size, &next_out_of_line, out_error_msg)) != ZX_OK)) {
    drop_all_handles();
    return status;
  }

  uint8_t* b = reinterpret_cast<uint8_t*>(bytes);
  for (uint32_t i = primary_size; i < next_out_of_line; i++) {
    if (b[i] != 0) {
      set_error("non-zero padding bytes detected");
      drop_all_handles();
      return ZX_ERR_INVALID_ARGS;
    }
  }

  FidlDecoder<Mode::Decode, WireFormatVersion, uint8_t> decoder(
      b, num_bytes, handles, handle_metadata, num_handles, next_out_of_line, out_error_msg,
      hlcpp_mode);
  fidl::Walk<WireFormatVersion>(decoder, type, DecodingPosition<uint8_t>{b});

  if (unlikely(decoder.status() != ZX_OK)) {
    drop_all_handles();
    return decoder.status();
  }
  if (unlikely(!decoder.DidConsumeAllBytes())) {
    set_error("message did not decode all provided bytes");
    drop_all_handles();
    return ZX_ERR_INVALID_ARGS;
  }
  if (unlikely(!decoder.DidConsumeAllHandles())) {
    set_error("message did not decode all provided handles");
    drop_all_handles();
    return ZX_ERR_INVALID_ARGS;
  }

  (void)FidlHandleCloseMany(decoder.unknown_handles(), decoder.unknown_handle_idx());
  return ZX_OK;
}

template <FidlWireFormatVersion WireFormatVersion>
zx_status_t fidl_decode_impl_handle_info(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                                         const zx_handle_info_t* handle_infos, uint32_t num_handles,
                                         const char** out_error_msg, bool hlcpp_mode) {
  if (!handle_infos) {
    return fidl_decode_impl<WireFormatVersion>(type, bytes, num_bytes, nullptr, nullptr,
                                               num_handles, out_error_msg, hlcpp_mode);
  }
  fidl_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl_channel_handle_metadata_t handle_metadata[ZX_CHANNEL_MAX_MSG_HANDLES];
  for (uint32_t i = 0; i < num_handles; i++) {
    handles[i] = handle_infos[i].handle;
    handle_metadata[i] = {
        .obj_type = handle_infos[i].type,
        .rights = handle_infos[i].rights,
    };
  }
  return fidl_decode_impl<WireFormatVersion>(type, bytes, num_bytes, handles, handle_metadata,
                                             num_handles, out_error_msg, hlcpp_mode);
}
}  // namespace

zx_status_t internal__fidl_decode_etc_hlcpp__v2__may_break(const fidl_type_t* type, void* bytes,
                                                           uint32_t num_bytes,
                                                           const zx_handle_info_t* handle_infos,
                                                           uint32_t num_handle_infos,
                                                           const char** error_msg_out) {
  return fidl_decode_impl_handle_info<FIDL_WIRE_FORMAT_VERSION_V2>(
      type, bytes, num_bytes, handle_infos, num_handle_infos, error_msg_out, true);
}

zx_status_t fidl_decode_etc(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                            const zx_handle_info_t* handle_infos, uint32_t num_handle_infos,
                            const char** error_msg_out) {
  return fidl_decode_impl_handle_info<FIDL_WIRE_FORMAT_VERSION_V2>(
      type, bytes, num_bytes, handle_infos, num_handle_infos, error_msg_out, false);
}

zx_status_t fidl_decode_msg(const fidl_type_t* type, fidl_incoming_msg_t* msg,
                            const char** out_error_msg) {
  zx_handle_info_t handle_infos[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl_channel_handle_metadata_t* metadata =
      reinterpret_cast<fidl_channel_handle_metadata_t*>(msg->handle_metadata);
  for (uint32_t i = 0; i < msg->num_handles; i++) {
    handle_infos[i] = {
        .handle = msg->handles[i],
        .type = metadata[i].obj_type,
        .rights = metadata[i].rights,
    };
    msg->handles[i] = ZX_HANDLE_INVALID;
  }

  uint8_t* trimmed_bytes;
  uint32_t trimmed_num_bytes;
  zx_status_t trim_status = ::fidl::internal::fidl_exclude_header_bytes(
      msg->bytes, msg->num_bytes, &trimmed_bytes, &trimmed_num_bytes, out_error_msg);
  if (unlikely(trim_status != ZX_OK)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (trimmed_num_bytes == 0) {
    return ZX_OK;
  }

  return fidl_decode_etc(type, trimmed_bytes, trimmed_num_bytes, handle_infos, msg->num_handles,
                         out_error_msg);
}

template <FidlWireFormatVersion WireFormatVersion>
zx_status_t fidl_validate_impl(const fidl_type_t* type, const void* bytes, uint32_t num_bytes,
                               uint32_t num_handles, const char** out_error_msg) {
  auto set_error = [&out_error_msg](const char* msg) {
    if (out_error_msg)
      *out_error_msg = msg;
  };
  if (unlikely(type == nullptr)) {
    set_error("fidl type cannot be null");
    return ZX_ERR_INVALID_ARGS;
  }
  if (bytes == nullptr) {
    set_error("Cannot validate null bytes");
    return ZX_ERR_INVALID_ARGS;
  }
  if (unlikely(!FidlIsAligned(reinterpret_cast<const uint8_t*>(bytes)))) {
    set_error("Bytes must be aligned to FIDL_ALIGNMENT");
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status;
  uint32_t primary_size;
  uint32_t next_out_of_line;
  if (unlikely((status = fidl::PrimaryObjectSize<WireFormatVersion>(
                    type, num_bytes, &primary_size, &next_out_of_line, out_error_msg)) != ZX_OK)) {
    return status;
  }

  const uint8_t* b = reinterpret_cast<const uint8_t*>(bytes);
  for (uint32_t i = primary_size; i < next_out_of_line; i++) {
    if (b[i] != 0) {
      set_error("non-zero padding bytes detected");
      return ZX_ERR_INVALID_ARGS;
    }
  }

  FidlDecoder<Mode::Validate, WireFormatVersion, const uint8_t> validator(
      b, num_bytes, nullptr, nullptr, num_handles, next_out_of_line, out_error_msg, false);
  fidl::Walk<WireFormatVersion>(validator, type, DecodingPosition<const uint8_t>{b});

  if (validator.status() == ZX_OK) {
    if (!validator.DidConsumeAllBytes()) {
      set_error("message did not consume all provided bytes");
      return ZX_ERR_INVALID_ARGS;
    }
    if (!validator.DidConsumeAllHandles()) {
      set_error("message did not reference all provided handles");
      return ZX_ERR_INVALID_ARGS;
    }
  }

  return validator.status();
}

zx_status_t internal__fidl_validate__v2__may_break(const fidl_type_t* type, const void* bytes,
                                                   uint32_t num_bytes, uint32_t num_handles,
                                                   const char** out_error_msg) {
  return fidl_validate_impl<FIDL_WIRE_FORMAT_VERSION_V2>(type, bytes, num_bytes, num_handles,
                                                         out_error_msg);
}
