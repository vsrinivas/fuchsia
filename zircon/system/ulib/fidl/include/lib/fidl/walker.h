// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_WALKER_H_
#define LIB_FIDL_WALKER_H_

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/visitor.h>
#include <stdalign.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <type_traits>

namespace fidl {

namespace internal {

// Some assumptions about data type layout.
static_assert(offsetof(fidl_string_t, size) == 0u, "fidl_string_t layout");
static_assert(offsetof(fidl_string_t, data) == 8u, "fidl_string_t layout");
static_assert(sizeof(fidl_string_t) == 16u, "fidl_string_t layout");

static_assert(offsetof(fidl_vector_t, count) == 0u, "fidl_vector_t layout");
static_assert(offsetof(fidl_vector_t, data) == 8u, "fidl_vector_t layout");
static_assert(sizeof(fidl_vector_t) == 16u, "fidl_vector_t layout");

static_assert(offsetof(fidl_envelope_t, num_bytes) == 0u, "fidl_envelope_t layout");
static_assert(offsetof(fidl_envelope_t, num_handles) == 4u, "fidl_envelope_t layout");
static_assert(offsetof(fidl_envelope_t, data) == 8u, "fidl_envelope_t layout");
static_assert(sizeof(fidl_envelope_t) == 16u, "fidl_envelope_t layout");

static_assert(offsetof(fidl_envelope_v2_t, num_bytes) == 0u, "fidl_envelope_v2_t layout");
static_assert(offsetof(fidl_envelope_v2_t, inline_value) == 0u, "fidl_envelope_v2_t layout");
static_assert(offsetof(fidl_envelope_v2_t, num_handles) == 4u, "fidl_envelope_v2_t layout");
static_assert(offsetof(fidl_envelope_v2_t, flags) == 6u, "fidl_envelope_v2_t layout");
static_assert(sizeof(fidl_envelope_v2_t) == 8u, "fidl_envelope_v2_t layout");

static_assert(ZX_HANDLE_INVALID == FIDL_HANDLE_ABSENT, "invalid handle equals absence marker");

#define FIDL_VERSIONED_VALUE(V1_VALUE, V2_VALUE) \
  ((WireFormatVersion == FIDL_WIRE_FORMAT_VERSION_V1) ? V1_VALUE : V2_VALUE)

constexpr uint32_t PrimitiveSize(const FidlCodedPrimitiveSubtype primitive) {
  switch (primitive) {
    case kFidlCodedPrimitiveSubtype_Bool:
    case kFidlCodedPrimitiveSubtype_Int8:
    case kFidlCodedPrimitiveSubtype_Uint8:
      return 1;
    case kFidlCodedPrimitiveSubtype_Int16:
    case kFidlCodedPrimitiveSubtype_Uint16:
      return 2;
    case kFidlCodedPrimitiveSubtype_Int32:
    case kFidlCodedPrimitiveSubtype_Uint32:
    case kFidlCodedPrimitiveSubtype_Float32:
      return 4;
    case kFidlCodedPrimitiveSubtype_Int64:
    case kFidlCodedPrimitiveSubtype_Uint64:
    case kFidlCodedPrimitiveSubtype_Float64:
      return 8;
  }
  __builtin_unreachable();
}

template <FidlWireFormatVersion WireFormatVersion>
constexpr uint32_t TypeSize(const fidl_type_t* type) {
  switch (type->type_tag()) {
    case kFidlTypePrimitive:
      return PrimitiveSize(type->coded_primitive().type);
    case kFidlTypeEnum:
      return PrimitiveSize(type->coded_enum().underlying_type);
    case kFidlTypeBits:
      return PrimitiveSize(type->coded_bits().underlying_type);
    case kFidlTypeStructPointer:
      return sizeof(uint64_t);
    case kFidlTypeHandle:
      return sizeof(zx_handle_t);
    case kFidlTypeStruct:
      return FIDL_VERSIONED_VALUE(type->coded_struct().size_v1, type->coded_struct().size_v2);
    case kFidlTypeTable:
      return sizeof(fidl_vector_t);
    case kFidlTypeXUnion:
      return FIDL_VERSIONED_VALUE(sizeof(fidl_xunion_t), sizeof(fidl_xunion_v2_t));
    case kFidlTypeString:
      return sizeof(fidl_string_t);
    case kFidlTypeArray:
      return FIDL_VERSIONED_VALUE(type->coded_array().array_size_v1,
                                  type->coded_array().array_size_v2);
    case kFidlTypeVector:
      return sizeof(fidl_vector_t);
  }
  __builtin_unreachable();
}

enum Result { kContinue, kExit };

// Macro to insert the relevant goop required to support two control flows here in case of error:
// one where we keep reading after error, and another where we return immediately.
#define FIDL_STATUS_GUARD(status)                             \
  if (unlikely((status) != Status::kSuccess)) {               \
    switch ((status)) {                                       \
      case Status::kSuccess:                                  \
        __builtin_unreachable();                              \
      case Status::kConstraintViolationError:                 \
        if (VisitorImpl::kContinueAfterConstraintViolation) { \
          return Result::kContinue;                           \
        } else {                                              \
          return Result::kExit;                               \
        }                                                     \
      case Status::kMemoryError:                              \
        return Result::kExit;                                 \
    }                                                         \
  }

// Variant of FIDL_STATUS_GUARD that continues in the same scope after a constraint violation,
// rather than exiting the current function and continuing in the next.
#define FIDL_CONTINUE_IN_SCOPE_STATUS_GUARD(status)           \
  if (unlikely((status) != Status::kSuccess)) {               \
    switch ((status)) {                                       \
      case Status::kSuccess:                                  \
        __builtin_unreachable();                              \
      case Status::kConstraintViolationError:                 \
        if (VisitorImpl::kContinueAfterConstraintViolation) { \
          break;                                              \
        } else {                                              \
          return Result::kExit;                               \
        }                                                     \
      case Status::kMemoryError:                              \
        return Result::kExit;                                 \
    }                                                         \
  }

// Macro to handle exiting if called function signaled exit.
#define FIDL_RESULT_GUARD(result)            \
  if (unlikely((result) == Result::kExit)) { \
    return Result::kExit;                    \
  }

typedef uint8_t OutOfLineDepth;

#define INCREASE_DEPTH(depth) OutOfLineDepth(depth + 1)

#define FIDL_DEPTH_GUARD(depth)                           \
  if (unlikely((depth) > FIDL_MAX_DEPTH)) {               \
    visitor_->OnError("recursion depth exceeded");        \
    if (VisitorImpl::kContinueAfterConstraintViolation) { \
      return Result::kContinue;                           \
    } else {                                              \
      return Result::kExit;                               \
    }                                                     \
  }

// The Walker class traverses through a FIDL message by following its coding table and
// calling the visitor implementation. VisitorImpl must be a concrete implementation of the
// fidl::Visitor interface. The concrete type is used to eliminate dynamic dispatch.
template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
class Walker final {
 private:
  using MutationTrait = typename VisitorImpl::MutationTrait;

  using Position = typename VisitorImpl::Position;

  using EnvelopeCheckpoint = typename VisitorImpl::EnvelopeCheckpoint;

  using EnvelopePointer = typename VisitorImpl::EnvelopePointer;

  using VisitorSuper = Visitor<WireFormatVersion, MutationTrait, Position, EnvelopeCheckpoint>;

  using Status = typename VisitorSuper::Status;

  static_assert(CheckVisitorInterface<VisitorSuper, VisitorImpl>(), "");

 public:
  Walker(VisitorImpl* visitor) : visitor_(visitor) {}

  Result Walk(const fidl_type_t* type, Position position);

 private:
  VisitorImpl* visitor_;

  // Optionally uses non-const pointers depending on if the visitor
  // is declared as mutating or not.
  template <typename T>
  using Ptr = typename VisitorImpl::template Ptr<T>;

  // Wrapper around Position::Get with friendlier syntax.
  template <typename T>
  Ptr<T> PtrTo(Position position) {
    return position.template Get<T>();
  }

  Result WalkInternal(const fidl_type_t* type, Position position, OutOfLineDepth depth);
  Result WalkIterableInternal(const fidl_type_t* type,
                              Walker<VisitorImpl, WireFormatVersion>::Position position,
                              uint32_t stride, uint32_t end_offset, OutOfLineDepth depth);
  static bool NeedWalkPrimitive(const FidlCodedPrimitiveSubtype subtype);
  Result WalkPrimitive(const FidlCodedPrimitive* fidl_coded_primitive, Position position);
  Result WalkEnum(const FidlCodedEnum* fidl_coded_enum, Position position);
  Result WalkBits(const FidlCodedBits* coded_bits, Position position);
  Result WalkStruct(const FidlCodedStruct* coded_struct, Position position, OutOfLineDepth depth);
  Result WalkStructPointer(const FidlCodedStructPointer* coded_struct, Position position,
                           OutOfLineDepth depth);
  Result WalkEnvelope(Position envelope_position, const fidl_type_t* payload_type,
                      OutOfLineDepth depth, FidlIsResource is_resource);
  Result WalkEnvelopeV1(Position envelope_position, const fidl_type_t* payload_type,
                        OutOfLineDepth depth, FidlIsResource is_resource);
  Result WalkEnvelopeV2(Position envelope_position, const fidl_type_t* payload_type,
                        OutOfLineDepth depth, FidlIsResource is_resource);
  Result WalkTable(const FidlCodedTable* coded_table, Position position, OutOfLineDepth depth);
  Result WalkXUnion(const FidlCodedXUnion* coded_table, Position position, OutOfLineDepth depth);
  Result WalkXUnionV1(const FidlCodedXUnion* coded_table, Position position, OutOfLineDepth depth);
  Result WalkXUnionV2(const FidlCodedXUnion* coded_table, Position position, OutOfLineDepth depth);
  Result WalkArray(const FidlCodedArray* coded_array, Position position, OutOfLineDepth depth);
  Result WalkString(const FidlCodedString* coded_string, Position position, OutOfLineDepth depth);
  Result WalkVector(const FidlCodedVector* coded_vector, Position position, OutOfLineDepth depth);
  Result WalkHandle(const FidlCodedHandle* coded_handle, Position position);
};

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::Walk(
    const fidl_type_t* type, Walker<VisitorImpl, WireFormatVersion>::Position position) {
  return WalkInternal(type, position, 0);
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkInternal(
    const fidl_type_t* type, Walker<VisitorImpl, WireFormatVersion>::Position position,
    OutOfLineDepth depth) {
  switch (type->type_tag()) {
    case kFidlTypePrimitive:
      return WalkPrimitive(&type->coded_primitive(), position);
    case kFidlTypeEnum:
      return WalkEnum(&type->coded_enum(), position);
    case kFidlTypeBits:
      return WalkBits(&type->coded_bits(), position);
    case kFidlTypeStruct:
      return WalkStruct(&type->coded_struct(), position, depth);
    case kFidlTypeStructPointer:
      return WalkStructPointer(&type->coded_struct_pointer(), position, depth);
    case kFidlTypeTable:
      return WalkTable(&type->coded_table(), position, depth);
    case kFidlTypeXUnion:
      return WalkXUnion(&type->coded_xunion(), position, depth);
    case kFidlTypeArray:
      return WalkArray(&type->coded_array(), position, depth);
    case kFidlTypeString:
      return WalkString(&type->coded_string(), position, depth);
    case kFidlTypeHandle:
      return WalkHandle(&type->coded_handle(), position);
    case kFidlTypeVector:
      return WalkVector(&type->coded_vector(), position, depth);
  }
  assert(false && "unhandled type");
  return Result::kContinue;
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkIterableInternal(
    const fidl_type_t* elem_type, Walker<VisitorImpl, WireFormatVersion>::Position position,
    uint32_t stride, uint32_t end_offset, OutOfLineDepth depth) {
  if (unlikely(!elem_type)) {
    // TODO(fxbug.dev/55226) Remove this case - it is only for tests.
    return Result::kContinue;
  }
  if (elem_type->type_tag() == kFidlTypePrimitive &&
      !NeedWalkPrimitive(elem_type->coded_primitive().type)) {
    return Result::kContinue;
  }
  for (uint32_t offset = 0; offset < end_offset; offset += stride) {
    auto result = WalkInternal(elem_type, position + offset, depth);
    FIDL_RESULT_GUARD(result);
  }
  return Result::kContinue;
}

// Keep in sync with WalkPrimitive.
template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
bool Walker<VisitorImpl, WireFormatVersion>::NeedWalkPrimitive(
    const FidlCodedPrimitiveSubtype subtype) {
  switch (subtype) {
    case kFidlCodedPrimitiveSubtype_Bool:
      return true;
    default:
      return false;
  }
}

// Keep in sync with NeedWalkPrimitive.
template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkPrimitive(
    const FidlCodedPrimitive* fidl_coded_primitive,
    Walker<VisitorImpl, WireFormatVersion>::Position position) {
  switch (fidl_coded_primitive->type) {
    case kFidlCodedPrimitiveSubtype_Bool: {
      uint8_t value = *PtrTo<uint8_t>(position);
      if (unlikely(value != 0 && value != 1)) {
        visitor_->OnError("not a valid bool value");
        FIDL_STATUS_GUARD(Status::kConstraintViolationError);
      }
      break;
    }
    default:
      break;
  }
  return Result::kContinue;
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkEnum(
    const FidlCodedEnum* coded_enum, Walker<VisitorImpl, WireFormatVersion>::Position position) {
  if (coded_enum->strictness == kFidlStrictness_Flexible) {
    return Result::kContinue;
  }

  uint64_t value;
  switch (coded_enum->underlying_type) {
    case kFidlCodedPrimitiveSubtype_Uint8:
      value = *PtrTo<uint8_t>(position);
      break;
    case kFidlCodedPrimitiveSubtype_Uint16:
      value = *PtrTo<uint16_t>(position);
      break;
    case kFidlCodedPrimitiveSubtype_Uint32:
      value = *PtrTo<uint32_t>(position);
      break;
    case kFidlCodedPrimitiveSubtype_Uint64:
      value = *PtrTo<uint64_t>(position);
      break;
    case kFidlCodedPrimitiveSubtype_Int8:
      value = static_cast<uint64_t>(*PtrTo<int8_t>(position));
      break;
    case kFidlCodedPrimitiveSubtype_Int16:
      value = static_cast<uint64_t>(*PtrTo<int16_t>(position));
      break;
    case kFidlCodedPrimitiveSubtype_Int32:
      value = static_cast<uint64_t>(*PtrTo<int32_t>(position));
      break;
    case kFidlCodedPrimitiveSubtype_Int64:
      value = static_cast<uint64_t>(*PtrTo<int64_t>(position));
      break;
    default:
      __builtin_unreachable();
  }
  if (unlikely(!coded_enum->validate(value))) {
    visitor_->OnError("not a valid enum member");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }
  return Result::kContinue;
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkBits(
    const FidlCodedBits* coded_bits, Walker<VisitorImpl, WireFormatVersion>::Position position) {
  if (coded_bits->strictness == kFidlStrictness_Flexible) {
    return Result::kContinue;
  }

  uint64_t value;
  switch (coded_bits->underlying_type) {
    case kFidlCodedPrimitiveSubtype_Uint8:
      value = *PtrTo<uint8_t>(position);
      break;
    case kFidlCodedPrimitiveSubtype_Uint16:
      value = *PtrTo<uint16_t>(position);
      break;
    case kFidlCodedPrimitiveSubtype_Uint32:
      value = *PtrTo<uint32_t>(position);
      break;
    case kFidlCodedPrimitiveSubtype_Uint64:
      value = *PtrTo<uint64_t>(position);
      break;
    default:
      __builtin_unreachable();
  }
  if (unlikely(value & ~coded_bits->mask)) {
    visitor_->OnError("not a valid bits member");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }
  return Result::kContinue;
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkStruct(
    const FidlCodedStruct* coded_struct, Walker<VisitorImpl, WireFormatVersion>::Position position,
    OutOfLineDepth depth) {
  for (uint32_t i = 0; i < coded_struct->element_count; i++) {
    const FidlStructElement& element = coded_struct->elements[i];
    if (VisitorImpl::kOnlyWalkResources) {
      if (!element.header.is_resource)
        continue;
    }
    switch (element.header.element_type) {
      case kFidlStructElementType_Field: {
        const FidlStructField& field = element.field;
        Position field_position = position + FIDL_VERSIONED_VALUE(field.offset_v1, field.offset_v2);
        Result result = WalkInternal(field.field_type, field_position, depth);
        FIDL_RESULT_GUARD(result);
        break;
      }
      case kFidlStructElementType_Padding64: {
        const FidlStructPadding& padding = element.padding;
        auto status = visitor_->VisitInternalPadding(
            position + FIDL_VERSIONED_VALUE(padding.offset_v1, padding.offset_v2), padding.mask_64);
        FIDL_STATUS_GUARD(status);
        break;
      }
      case kFidlStructElementType_Padding32: {
        const FidlStructPadding& padding = element.padding;
        auto status = visitor_->VisitInternalPadding(
            position + FIDL_VERSIONED_VALUE(padding.offset_v1, padding.offset_v2), padding.mask_32);
        FIDL_STATUS_GUARD(status);
        break;
      }
      case kFidlStructElementType_Padding16: {
        const FidlStructPadding& padding = element.padding;
        auto status = visitor_->VisitInternalPadding(
            position + FIDL_VERSIONED_VALUE(padding.offset_v1, padding.offset_v2), padding.mask_16);
        FIDL_STATUS_GUARD(status);
        break;
      }
    }
  }
  return Result::kContinue;
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkStructPointer(
    const FidlCodedStructPointer* coded_struct_pointer,
    Walker<VisitorImpl, WireFormatVersion>::Position position, OutOfLineDepth depth) {
  if (*PtrTo<Ptr<void>>(position) == nullptr) {
    return Result::kContinue;
  }

  OutOfLineDepth inner_depth = INCREASE_DEPTH(depth);
  FIDL_DEPTH_GUARD(inner_depth);
  Position obj_position;
  auto status =
      visitor_->VisitPointer(position, VisitorImpl::PointeeType::kOther, PtrTo<Ptr<void>>(position),
                             FIDL_VERSIONED_VALUE(coded_struct_pointer->struct_type->size_v1,
                                                  coded_struct_pointer->struct_type->size_v2),
                             kFidlMemcpyCompatibility_CannotMemcpy, &obj_position);
  FIDL_STATUS_GUARD(status);
  return WalkStruct(coded_struct_pointer->struct_type, obj_position, inner_depth);
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkEnvelope(Position envelope_position,
                                                            const fidl_type_t* payload_type,
                                                            OutOfLineDepth depth,
                                                            bool is_resource) {
  switch (WireFormatVersion) {
    case FIDL_WIRE_FORMAT_VERSION_V1:
      return WalkEnvelopeV1(envelope_position, payload_type, depth, is_resource);
    case FIDL_WIRE_FORMAT_VERSION_V2:
      return WalkEnvelopeV2(envelope_position, payload_type, depth, is_resource);
  }
  __builtin_unreachable();
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkEnvelopeV1(Position envelope_position,
                                                              const fidl_type_t* payload_type,
                                                              OutOfLineDepth depth,
                                                              bool is_resource) {
  auto envelope = PtrTo<std::remove_pointer_t<EnvelopePointer>>(envelope_position);
  auto v1_envelope = reinterpret_cast<const fidl_envelope_t*>(envelope);

  EnvelopeCheckpoint checkpoint = visitor_->EnterEnvelope();

  if (v1_envelope->data != nullptr) {
    OutOfLineDepth obj_depth = INCREASE_DEPTH(depth);
    FIDL_DEPTH_GUARD(obj_depth);

    uint32_t num_bytes = payload_type != nullptr ? TypeSize<WireFormatVersion>(payload_type)
                                                 : v1_envelope->num_bytes;
    Position obj_position;
    auto status = visitor_->VisitPointer(envelope_position, VisitorImpl::PointeeType::kOther,
                                         // casting since |envelope_ptr->data| is always void*
                                         &const_cast<Ptr<void>&>(v1_envelope->data), num_bytes,
                                         kFidlMemcpyCompatibility_CannotMemcpy, &obj_position);
    FIDL_CONTINUE_IN_SCOPE_STATUS_GUARD(status);

    if (likely(payload_type != nullptr)) {
      auto result = WalkInternal(payload_type, obj_position, obj_depth);
      FIDL_RESULT_GUARD(result);
    } else {
      status = visitor_->VisitUnknownEnvelope(envelope, is_resource);
      FIDL_STATUS_GUARD(status);
    }
  }

  auto status = visitor_->LeaveEnvelope(envelope, checkpoint);
  FIDL_STATUS_GUARD(status);

  return Result::kContinue;
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkEnvelopeV2(Position envelope_position,
                                                              const fidl_type_t* payload_type,
                                                              OutOfLineDepth depth,
                                                              bool is_resource) {
  auto envelope = PtrTo<std::remove_pointer_t<EnvelopePointer>>(envelope_position);
  auto v2_envelope = reinterpret_cast<const fidl_envelope_v2_t*>(envelope);

  EnvelopeCheckpoint checkpoint = visitor_->EnterEnvelope();

  bool is_inlined = v2_envelope->flags & FIDL_ENVELOPE_FLAGS_INLINING_MASK;
  if (payload_type != nullptr) {
    bool should_be_inlined =
        TypeSize<WireFormatVersion>(payload_type) <= FIDL_ENVELOPE_INLINING_SIZE_THRESHOLD;
    if (unlikely(is_inlined != should_be_inlined)) {
      visitor_->OnError("Invalid inline bit in envelope");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
  }

  if (is_inlined) {
    OutOfLineDepth obj_depth = INCREASE_DEPTH(depth);
    FIDL_DEPTH_GUARD(obj_depth);

    if (payload_type != nullptr) {
      auto result = WalkInternal(payload_type, envelope_position, obj_depth);
      FIDL_RESULT_GUARD(result);
    } else {
      auto status = visitor_->VisitUnknownEnvelope(envelope, is_resource);
      FIDL_STATUS_GUARD(status);
    }
    return Result::kContinue;
  }

  if (v2_envelope->num_bytes != 0 || v2_envelope->num_handles != 0) {
    OutOfLineDepth obj_depth = INCREASE_DEPTH(depth);
    FIDL_DEPTH_GUARD(obj_depth);

    uint32_t num_bytes = payload_type != nullptr ? TypeSize<WireFormatVersion>(payload_type)
                                                 : v2_envelope->num_bytes;
    Position obj_position;
    auto status = visitor_->VisitPointer(envelope_position, VisitorImpl::PointeeType::kOther,
                                         PtrTo<void*>(envelope_position), num_bytes,
                                         kFidlMemcpyCompatibility_CannotMemcpy, &obj_position);
    FIDL_CONTINUE_IN_SCOPE_STATUS_GUARD(status);

    if (likely(payload_type != nullptr)) {
      auto result = WalkInternal(payload_type, obj_position, obj_depth);
      FIDL_RESULT_GUARD(result);
    } else {
      status = visitor_->VisitUnknownEnvelope(envelope, is_resource);
      FIDL_STATUS_GUARD(status);
    }
  }

  auto status = visitor_->LeaveEnvelope(envelope, checkpoint);
  FIDL_STATUS_GUARD(status);

  return Result::kContinue;
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkTable(
    const FidlCodedTable* coded_table, Walker<VisitorImpl, WireFormatVersion>::Position position,
    OutOfLineDepth depth) {
  uint32_t envelope_size =
      FIDL_VERSIONED_VALUE(sizeof(fidl_envelope_t), sizeof(fidl_envelope_v2_t));
  auto envelope_vector_ptr = PtrTo<fidl_vector_t>(position);
  if (envelope_vector_ptr->data == nullptr) {
    if (unlikely(envelope_vector_ptr->count != 0)) {
      visitor_->OnError("Table envelope vector data absent but non-zero count");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
    OutOfLineDepth array_depth = INCREASE_DEPTH(depth);
    FIDL_DEPTH_GUARD(array_depth);
    auto status = visitor_->VisitAbsentPointerInNonNullableCollection(&envelope_vector_ptr->data);
    FIDL_STATUS_GUARD(status);
    return Result::kContinue;
  }

  uint32_t size;
  if (unlikely(mul_overflow(envelope_vector_ptr->count, envelope_size, &size))) {
    visitor_->OnError("integer overflow calculating table size");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }
  OutOfLineDepth envelope_vector_depth = INCREASE_DEPTH(depth);
  FIDL_DEPTH_GUARD(envelope_vector_depth);
  Position envelope_vector_position;
  auto status = visitor_->VisitPointer(
      position, VisitorImpl::PointeeType::kOther, &envelope_vector_ptr->data, size,
      kFidlMemcpyCompatibility_CannotMemcpy, &envelope_vector_position);
  FIDL_STATUS_GUARD(status);

  const FidlTableField* next_field = coded_table->fields;
  const FidlTableField* end_field = coded_table->fields + coded_table->field_count;
  for (uint32_t field_index = 0; field_index < envelope_vector_ptr->count; field_index++) {
    const uint32_t ordinal = field_index + 1;
    const FidlTableField* known_field = nullptr;
    if (next_field < end_field && next_field->ordinal == ordinal) {
      known_field = next_field;
      next_field++;
    }
    Position envelope_position = envelope_vector_position + field_index * envelope_size;
    const fidl_type_t* payload_type = known_field ? known_field->type : nullptr;
    auto result = WalkEnvelope(envelope_position, payload_type, envelope_vector_depth,
                               coded_table->is_resource);
    FIDL_RESULT_GUARD(result);
  }
  return Result::kContinue;
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkXUnion(
    const FidlCodedXUnion* coded_xunion, Walker<VisitorImpl, WireFormatVersion>::Position position,
    OutOfLineDepth depth) {
  switch (WireFormatVersion) {
    case FIDL_WIRE_FORMAT_VERSION_V1:
      return WalkXUnionV1(coded_xunion, position, depth);
    case FIDL_WIRE_FORMAT_VERSION_V2:
      return WalkXUnionV2(coded_xunion, position, depth);
  }
  __builtin_unreachable();
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkXUnionV1(
    const FidlCodedXUnion* coded_xunion, Walker<VisitorImpl, WireFormatVersion>::Position position,
    OutOfLineDepth depth) {
  auto xunion = PtrTo<fidl_xunion_t>(position);
  const auto envelope_pos = position + offsetof(fidl_xunion_t, envelope);
  auto envelope_ptr = &xunion->envelope;

  // Validate zero-ordinal invariants
  if (xunion->tag == 0) {
    if (unlikely(envelope_ptr->data != nullptr || envelope_ptr->num_bytes != 0 ||
                 envelope_ptr->num_handles != 0)) {
      visitor_->OnError("xunion with zero as ordinal must be empty");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
    if (unlikely(!coded_xunion->nullable)) {
      visitor_->OnError("non-nullable xunion is absent");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
    return Result::kContinue;
  } else if (unlikely(envelope_ptr->data == nullptr)) {
    visitor_->OnError("empty xunion must have zero as ordinal");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }

  const fidl_type_t* payload_type = nullptr;
  if (xunion->tag <= coded_xunion->field_count) {
    payload_type = coded_xunion->fields[xunion->tag - 1].type;
  }
  if (unlikely(payload_type == nullptr && coded_xunion->strictness == kFidlStrictness_Strict)) {
    visitor_->OnError("strict xunion has unknown ordinal");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }

  return WalkEnvelopeV1(envelope_pos, payload_type, depth, coded_xunion->is_resource);
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkXUnionV2(
    const FidlCodedXUnion* coded_xunion, Walker<VisitorImpl, WireFormatVersion>::Position position,
    OutOfLineDepth depth) {
  auto xunion = PtrTo<fidl_xunion_v2_t>(position);
  const auto envelope_pos = position + offsetof(fidl_xunion_v2_t, envelope);
  auto envelope_ptr = &xunion->envelope;

  // Validate zero-ordinal invariants
  if (xunion->tag == 0) {
    if (unlikely(envelope_ptr->num_bytes != 0 || envelope_ptr->num_handles != 0 ||
                 envelope_ptr->flags != 0)) {
      visitor_->OnError("xunion with zero as ordinal must be empty");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
    if (unlikely(!coded_xunion->nullable)) {
      visitor_->OnError("non-nullable xunion is absent");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
    return Result::kContinue;
  } else if (unlikely(envelope_ptr->num_bytes == 0 && envelope_ptr->num_handles == 0 &&
                      envelope_ptr->flags == 0)) {
    visitor_->OnError("empty xunion must have zero as ordinal");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }

  const fidl_type_t* payload_type = nullptr;
  if (xunion->tag <= coded_xunion->field_count) {
    payload_type = coded_xunion->fields[xunion->tag - 1].type;
  }
  if (unlikely(payload_type == nullptr && coded_xunion->strictness == kFidlStrictness_Strict)) {
    visitor_->OnError("strict xunion has unknown ordinal");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }

  return WalkEnvelopeV2(envelope_pos, payload_type, depth, coded_xunion->is_resource);
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkArray(
    const FidlCodedArray* coded_array, Walker<VisitorImpl, WireFormatVersion>::Position position,
    OutOfLineDepth depth) {
  return WalkIterableInternal(
      coded_array->element, position,
      FIDL_VERSIONED_VALUE(coded_array->element_size_v1, coded_array->element_size_v2),
      FIDL_VERSIONED_VALUE(coded_array->array_size_v1, coded_array->array_size_v2), depth);
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkString(
    const FidlCodedString* coded_string, Walker<VisitorImpl, WireFormatVersion>::Position position,
    OutOfLineDepth depth) {
  auto string_ptr = PtrTo<fidl_string_t>(position);
  const uint64_t size = string_ptr->size;
  auto status = visitor_->VisitVectorOrStringCount(&string_ptr->size);
  FIDL_STATUS_GUARD(status);
  if (string_ptr->data == nullptr) {
    if (unlikely(size != 0)) {
      visitor_->OnError("string is absent but length is not zero");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
    if (unlikely(!coded_string->nullable)) {
      OutOfLineDepth array_depth = INCREASE_DEPTH(depth);
      FIDL_DEPTH_GUARD(array_depth);
      status = visitor_->VisitAbsentPointerInNonNullableCollection(
          &reinterpret_cast<Ptr<void>&>(const_cast<Ptr<char>&>(string_ptr->data)));
      FIDL_STATUS_GUARD(status);
    }
    return Result::kContinue;
  }

  uint64_t bound = coded_string->max_size;
  if (unlikely(size > std::numeric_limits<uint32_t>::max())) {
    visitor_->OnError("string size overflows 32 bits");
    FIDL_STATUS_GUARD(Status::kMemoryError);
  }
  if (unlikely(size > bound)) {
    visitor_->OnError("message tried to access too large of a bounded string");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }
  OutOfLineDepth array_depth = INCREASE_DEPTH(depth);
  FIDL_DEPTH_GUARD(array_depth);
  Position array_position;
  status = visitor_->VisitPointer(
      position, VisitorImpl::PointeeType::kString,
      &reinterpret_cast<Ptr<void>&>(const_cast<Ptr<char>&>(string_ptr->data)),
      static_cast<uint32_t>(size), kFidlMemcpyCompatibility_CanMemcpy, &array_position);
  FIDL_STATUS_GUARD(status);
  return Result::kContinue;
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkHandle(
    const FidlCodedHandle* coded_handle,
    Walker<VisitorImpl, WireFormatVersion>::Position position) {
  auto handle_ptr = PtrTo<zx_handle_t>(position);
  if (*handle_ptr == ZX_HANDLE_INVALID) {
    if (unlikely(!coded_handle->nullable)) {
      visitor_->OnError("message is missing a non-nullable handle");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
    return Result::kContinue;
  }
  auto status = visitor_->VisitHandle(position, handle_ptr, coded_handle->handle_rights,
                                      coded_handle->handle_subtype);
  FIDL_STATUS_GUARD(status);
  return Result::kContinue;
}

template <typename VisitorImpl, FidlWireFormatVersion WireFormatVersion>
Result Walker<VisitorImpl, WireFormatVersion>::WalkVector(
    const FidlCodedVector* coded_vector, Walker<VisitorImpl, WireFormatVersion>::Position position,
    OutOfLineDepth depth) {
  auto vector_ptr = PtrTo<fidl_vector_t>(position);
  const uint64_t count = vector_ptr->count;
  auto status = visitor_->VisitVectorOrStringCount(&vector_ptr->count);
  FIDL_STATUS_GUARD(status);

  if (unlikely(vector_ptr->data == nullptr)) {
    if (unlikely(count != 0)) {
      visitor_->OnError("absent vector of non-zero elements");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
    if (unlikely(!coded_vector->nullable)) {
      OutOfLineDepth array_depth = INCREASE_DEPTH(depth);
      FIDL_DEPTH_GUARD(array_depth);
      status = visitor_->VisitAbsentPointerInNonNullableCollection(&vector_ptr->data);
      FIDL_STATUS_GUARD(status);
    }
    return Result::kContinue;
  }

  if (unlikely(count > coded_vector->max_count)) {
    visitor_->OnError("message tried to access too large of a bounded vector");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }
  uint32_t size;
  if (unlikely(mul_overflow(
          count, FIDL_VERSIONED_VALUE(coded_vector->element_size_v1, coded_vector->element_size_v2),
          &size))) {
    visitor_->OnError("integer overflow calculating vector size");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }
  OutOfLineDepth array_depth = INCREASE_DEPTH(depth);
  FIDL_DEPTH_GUARD(array_depth);
  Position array_position;
  status =
      visitor_->VisitPointer(position, VisitorImpl::PointeeType::kVector, &vector_ptr->data, size,
                             coded_vector->element_memcpy_compatibility, &array_position);
  FIDL_STATUS_GUARD(status);

  uint32_t stride =
      FIDL_VERSIONED_VALUE(coded_vector->element_size_v1, coded_vector->element_size_v2);
  ZX_ASSERT(count <= std::numeric_limits<uint32_t>::max());
  uint32_t end_offset = uint32_t(count) * stride;
  return WalkIterableInternal(coded_vector->element, array_position, stride, end_offset,
                              array_depth);
}

}  // namespace internal

// Walks the FIDL message, calling hooks in the concrete VisitorImpl.
//
// |visitor|        is an implementation of the fidl::Visitor interface.
// |type|           is the coding table for the FIDL type. It cannot be null.
// |start|          is the starting point for the walk.
template <FidlWireFormatVersion WireFormatVersion, typename VisitorImpl>
void Walk(VisitorImpl& visitor, const fidl_type_t* type, typename VisitorImpl::Position start) {
  internal::Walker<VisitorImpl, WireFormatVersion> walker(&visitor);
  walker.Walk(type, start);
}

// Infer the size of the primary object, from the coding table in |type|.
// Ensures that the primary object is of one of the expected types.
// This outputs both the primary size of the first inline object but also the
// position of the out of line object (if any) that follows the inline object.
//
// This assumes the following properties of the input parameters:
// - |type|, |out_primary_size|, |out_next_out_of_line| are non-null
// - |out_err| can optionally be null
//
// An error is returned if:
// - The primary object is neither a struct nor a table.
// - The first out-of-line offset is larger than the size of the buffer.
// - The aligned first out-of-line offset overflows 32 bits.

template <FidlWireFormatVersion WireFormatVersion>
zx_status_t PrimaryObjectSize(const fidl_type_t* type, uint32_t buffer_size,
                              uint32_t* out_primary_size, uint32_t* out_first_out_of_line,
                              const char** out_error) {
  ZX_DEBUG_ASSERT(type != nullptr);
  ZX_DEBUG_ASSERT(out_primary_size != nullptr);
  ZX_DEBUG_ASSERT(out_first_out_of_line != nullptr);
  auto set_error = [&out_error](const char* msg) {
    if (out_error)
      *out_error = msg;
  };

  // The struct case is "likely" because overhead for tables is less of a relative cost.
  uint32_t primary_size;
  if (likely(type->type_tag() == kFidlTypeStruct)) {
    primary_size = FIDL_VERSIONED_VALUE(type->coded_struct().size_v1, type->coded_struct().size_v2);
  } else if (likely(type->type_tag() == kFidlTypeTable)) {
    primary_size = sizeof(fidl_table_t);
  } else {
    set_error("Message must be a struct or a table");
    return ZX_ERR_INVALID_ARGS;
  }
  *out_primary_size = static_cast<uint32_t>(primary_size);

  uint64_t first_out_of_line = FidlAlign(static_cast<uint32_t>(primary_size));
  if (unlikely(first_out_of_line > buffer_size)) {
    set_error("Buffer is too small for first inline object");
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if (unlikely(first_out_of_line > std::numeric_limits<uint32_t>::max())) {
    set_error("Out of line starting offset overflows");
    return ZX_ERR_INVALID_ARGS;
  }
  *out_first_out_of_line = static_cast<uint32_t>(first_out_of_line);
  return ZX_OK;
}

}  // namespace fidl

#endif  // LIB_FIDL_WALKER_H_
