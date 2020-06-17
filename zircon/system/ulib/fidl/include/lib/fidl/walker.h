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

// The MSB is an ownership bit in the count field of vectors.
constexpr uint64_t kVectorOwnershipMask = uint64_t(1) << 63;
// The LSB is an ownership bit in tracking_ptr of non-array type.
constexpr uint64_t kNonArrayTrackingPtrOwnershipMask = uint64_t(1) << 0;

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

static_assert(ZX_HANDLE_INVALID == FIDL_HANDLE_ABSENT, "invalid handle equals absence marker");

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
      return type->coded_struct().size;
    case kFidlTypeTable:
      return sizeof(fidl_vector_t);
    case kFidlTypeXUnion:
      return sizeof(fidl_xunion_t);
    case kFidlTypeString:
      return sizeof(fidl_string_t);
    case kFidlTypeArray:
      return type->coded_array().array_size;
    case kFidlTypeVector:
      return sizeof(fidl_vector_t);
  }
  __builtin_unreachable();
}

enum Result { kContinue, kExit };

// Macro to insert the relevant goop required to support two control flows here in case of error:
// one where we keep reading after error, and another where we return immediately.
#define FIDL_STATUS_GUARD(status)                           \
  switch ((status)) {                                       \
    case Status::kSuccess:                                  \
      break;                                                \
    case Status::kConstraintViolationError:                 \
      if (VisitorImpl::kContinueAfterConstraintViolation) { \
        return Result::kContinue;                           \
      } else {                                              \
        return Result::kExit;                               \
      }                                                     \
    case Status::kMemoryError:                              \
      return Result::kExit;                                 \
  }

// Macro to handle exiting if called function signaled exit.
#define FIDL_RESULT_GUARD(result) \
  switch ((result)) {             \
    case Result::kContinue:       \
      break;                      \
    case Result::kExit:           \
      return Result::kExit;       \
  }

typedef uint8_t OutOfLineDepth;

#define INCREASE_DEPTH(depth) OutOfLineDepth(depth + 1)

#define FIDL_DEPTH_GUARD(depth)                           \
  if ((depth) > FIDL_MAX_DEPTH) {                         \
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
template <typename VisitorImpl>
class Walker final {
 private:
  using MutationTrait = typename VisitorImpl::MutationTrait;

  using Position = typename VisitorImpl::Position;

  using VisitorSuper = Visitor<MutationTrait, Position>;

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
  Result WalkIterableInternal(const fidl_type_t* type, Walker<VisitorImpl>::Position position,
                              uint32_t stride, uint32_t end_offset, OutOfLineDepth depth);
  static bool NeedWalkPrimitive(const FidlCodedPrimitiveSubtype subtype);
  Result WalkPrimitive(const FidlCodedPrimitive* fidl_coded_primitive, Position position);
  Result WalkEnum(const FidlCodedEnum* fidl_coded_enum, Position position);
  Result WalkBits(const FidlCodedBits* coded_bits, Position position);
  Result WalkStruct(const FidlCodedStruct* coded_struct, Position position, OutOfLineDepth depth);
  Result WalkStructPointer(const FidlCodedStructPointer* coded_struct, Position position,
                           OutOfLineDepth depth);
  Result WalkTable(const FidlCodedTable* coded_table, Position position, OutOfLineDepth depth);
  Result WalkXUnion(const FidlCodedXUnion* coded_table, Position position, OutOfLineDepth depth);
  Result WalkArray(const FidlCodedArray* coded_array, Position position, OutOfLineDepth depth);
  Result WalkString(const FidlCodedString* coded_string, Position position, OutOfLineDepth depth);
  Result WalkVector(const FidlCodedVector* coded_vector, Position position, OutOfLineDepth depth);
  Result WalkHandle(const FidlCodedHandle* coded_handle, Position position);
};

template <typename VisitorImpl>
Result Walker<VisitorImpl>::Walk(const fidl_type_t* type, Walker<VisitorImpl>::Position position) {
  return WalkInternal(type, position, 0);
}

template <typename VisitorImpl>
Result Walker<VisitorImpl>::WalkInternal(const fidl_type_t* type,
                                         Walker<VisitorImpl>::Position position,
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

template <typename VisitorImpl>
Result Walker<VisitorImpl>::WalkIterableInternal(const fidl_type_t* elem_type,
                                                 Walker<VisitorImpl>::Position position,
                                                 uint32_t stride, uint32_t end_offset,
                                                 OutOfLineDepth depth) {
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
template <typename VisitorImpl>
bool Walker<VisitorImpl>::NeedWalkPrimitive(const FidlCodedPrimitiveSubtype subtype) {
  switch (subtype) {
    case kFidlCodedPrimitiveSubtype_Bool:
      return true;
    default:
      return false;
  }
}

// Keep in sync with NeedWalkPrimitive.
template <typename VisitorImpl>
Result Walker<VisitorImpl>::WalkPrimitive(const FidlCodedPrimitive* fidl_coded_primitive,
                                          Walker<VisitorImpl>::Position position) {
  switch (fidl_coded_primitive->type) {
    case kFidlCodedPrimitiveSubtype_Bool: {
      uint8_t value = *PtrTo<uint8_t>(position);
      if (value != 0 && value != 1) {
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

template <typename VisitorImpl>
Result Walker<VisitorImpl>::WalkEnum(const FidlCodedEnum* fidl_coded_enum,
                                     Walker<VisitorImpl>::Position position) {
  uint64_t value;
  switch (fidl_coded_enum->underlying_type) {
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
  if (!fidl_coded_enum->validate(value)) {
    // TODO(FIDL-523): Make this strictness dependent.
    visitor_->OnError("not a valid enum member");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }
  return Result::kContinue;
}

template <typename VisitorImpl>
Result Walker<VisitorImpl>::WalkBits(const FidlCodedBits* coded_bits,
                                     Walker<VisitorImpl>::Position position) {
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
  if (value & ~coded_bits->mask) {
    visitor_->OnError("not a valid bits member");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }
  return Result::kContinue;
}

template <typename VisitorImpl>
Result Walker<VisitorImpl>::WalkStruct(const FidlCodedStruct* coded_struct,
                                       Walker<VisitorImpl>::Position position,
                                       OutOfLineDepth depth) {
  for (uint32_t i = 0; i < coded_struct->field_count; i++) {
    const FidlStructField& field = coded_struct->fields[i];
    if (field.type) {
      // Field has a value.
      Position field_position = position + field.offset;
      if (field.padding > 0) {
        Position padding_position = field_position + TypeSize(field.type);
        auto status = visitor_->VisitInternalPadding(padding_position, field.padding);
        FIDL_STATUS_GUARD(status);
      }
      Result result = WalkInternal(field.type, field_position, depth);
      FIDL_RESULT_GUARD(result);
    } else if (field.padding > 0) {
      // Field entry is a padding marker, not an actual field.
      // The field offset is effectively the padding marker.
      auto status = visitor_->VisitInternalPadding(position + field.padding_offset, field.padding);
      FIDL_STATUS_GUARD(status);
    }
  }
  return Result::kContinue;
}

template <typename VisitorImpl>
Result Walker<VisitorImpl>::WalkStructPointer(const FidlCodedStructPointer* coded_struct_pointer,
                                              Walker<VisitorImpl>::Position position,
                                              OutOfLineDepth depth) {
  if (*PtrTo<Ptr<void>>(position) == nullptr) {
    return Result::kContinue;
  }
  OutOfLineDepth inner_depth = INCREASE_DEPTH(depth);
  FIDL_DEPTH_GUARD(inner_depth);
  Position obj_position;
  auto status =
      visitor_->VisitPointer(position, VisitorImpl::PointeeType::kOther, PtrTo<Ptr<void>>(position),
                             coded_struct_pointer->struct_type->size, &obj_position);
  FIDL_STATUS_GUARD(status);
  return WalkStruct(coded_struct_pointer->struct_type, obj_position, inner_depth);
}

template <typename VisitorImpl>
Result Walker<VisitorImpl>::WalkTable(const FidlCodedTable* coded_table,
                                      Walker<VisitorImpl>::Position position,
                                      OutOfLineDepth depth) {
  auto envelope_vector_ptr = PtrTo<fidl_vector_t>(position);
  if (envelope_vector_ptr->data == nullptr) {
    // The vector of envelope headers in a table is always non-nullable.
    if (!VisitorImpl::kAllowNonNullableCollectionsToBeAbsent) {
      visitor_->OnError("Table data cannot be absent");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
    if (envelope_vector_ptr->count != 0) {
      visitor_->OnError("Table envelope vector data absent but non-zero count");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
  }
  uint32_t size;
  if (mul_overflow(envelope_vector_ptr->count, sizeof(fidl_envelope_t), &size)) {
    visitor_->OnError("integer overflow calculating table size");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }
  OutOfLineDepth envelope_vector_depth = INCREASE_DEPTH(depth);
  FIDL_DEPTH_GUARD(envelope_vector_depth);
  Position envelope_vector_position;
  auto status = visitor_->VisitPointer(position, VisitorImpl::PointeeType::kOther,
                                       &envelope_vector_ptr->data, size, &envelope_vector_position);
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
    Position envelope_position =
        envelope_vector_position + field_index * uint32_t(sizeof(fidl_envelope_t));
    auto envelope_ptr = PtrTo<fidl_envelope_t>(envelope_position);

    const fidl_type_t* payload_type = known_field ? known_field->type : nullptr;
    status = visitor_->EnterEnvelope(envelope_position, envelope_ptr, payload_type);
    FIDL_STATUS_GUARD(status);

    if (envelope_ptr->data != nullptr) {
      uint32_t num_bytes =
          payload_type != nullptr ? TypeSize(payload_type) : envelope_ptr->num_bytes;
      OutOfLineDepth obj_depth = INCREASE_DEPTH(envelope_vector_depth);
      FIDL_DEPTH_GUARD(obj_depth);
      Position obj_position;
      status = visitor_->VisitPointer(envelope_position, VisitorImpl::PointeeType::kOther,
                                      // casting since |envelope_ptr->data| is always void*
                                      &const_cast<Ptr<void>&>(envelope_ptr->data), num_bytes,
                                      &obj_position);
      if (status == Status::kMemoryError || (status == Status::kConstraintViolationError &&
                                             !VisitorImpl::kContinueAfterConstraintViolation)) {
        return Result::kExit;
      }
      if (payload_type != nullptr) {
        auto result = WalkInternal(payload_type, obj_position, obj_depth);
        FIDL_RESULT_GUARD(result);
      }
    }

    status = visitor_->LeaveEnvelope(envelope_position, envelope_ptr);
    FIDL_STATUS_GUARD(status);
  }
  return Result::kContinue;
}

template <typename VisitorImpl>
Result Walker<VisitorImpl>::WalkXUnion(const FidlCodedXUnion* coded_xunion,
                                       Walker<VisitorImpl>::Position position,
                                       OutOfLineDepth depth) {
  auto xunion = PtrTo<fidl_xunion_t>(position);
  const auto envelope_pos = position + offsetof(fidl_xunion_t, envelope);
  auto envelope_ptr = &xunion->envelope;

  // Validate zero-ordinal invariants
  if (xunion->tag == 0) {
    if (envelope_ptr->data != nullptr || envelope_ptr->num_bytes != 0 ||
        envelope_ptr->num_handles != 0) {
      visitor_->OnError("xunion with zero as ordinal must be empty");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
    if (!coded_xunion->nullable) {
      visitor_->OnError("non-nullable xunion is absent");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
    return Result::kContinue;
  }

  const fidl_type_t* payload_type = nullptr;
  if (xunion->tag <= coded_xunion->field_count) {
    payload_type = coded_xunion->fields[xunion->tag - 1].type;
  }
  if (payload_type == nullptr && coded_xunion->strictness == kFidlStrictness_Strict) {
    visitor_->OnError("strict xunion has unknown ordinal");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }

  // Make sure we don't process a malformed envelope
  auto status = visitor_->EnterEnvelope(envelope_pos, envelope_ptr, payload_type);
  FIDL_STATUS_GUARD(status);
  // Skip empty envelopes
  if (envelope_ptr->data == nullptr) {
    if (xunion->tag != 0) {
      visitor_->OnError("empty xunion must have zero as ordinal");
      if (status == Status::kMemoryError || (status == Status::kConstraintViolationError &&
                                             !VisitorImpl::kContinueAfterConstraintViolation)) {
        return Result::kExit;
      }
    }
  } else {
    uint32_t num_bytes = payload_type != nullptr ? TypeSize(payload_type) : envelope_ptr->num_bytes;
    OutOfLineDepth obj_depth = INCREASE_DEPTH(depth);
    FIDL_DEPTH_GUARD(obj_depth);
    Position obj_position;
    status = visitor_->VisitPointer(position, VisitorImpl::PointeeType::kOther,
                                    &const_cast<Ptr<void>&>(envelope_ptr->data), num_bytes,
                                    &obj_position);
    if (status == Status::kMemoryError || (status == Status::kConstraintViolationError &&
                                           !VisitorImpl::kContinueAfterConstraintViolation)) {
      return Result::kExit;
    }
    if (payload_type != nullptr) {
      auto result = WalkInternal(payload_type, obj_position, obj_depth);
      FIDL_RESULT_GUARD(result);
    }
  }
  status = visitor_->LeaveEnvelope(envelope_pos, envelope_ptr);
  FIDL_STATUS_GUARD(status);
  return Result::kContinue;
}

template <typename VisitorImpl>
Result Walker<VisitorImpl>::WalkArray(const FidlCodedArray* coded_array,
                                      Walker<VisitorImpl>::Position position,
                                      OutOfLineDepth depth) {
  if (coded_array->element) {
    return WalkIterableInternal(coded_array->element, position, coded_array->element_size,
                                coded_array->array_size, depth);
  }
  return Result::kContinue;
}

template <typename VisitorImpl>
Result Walker<VisitorImpl>::WalkString(const FidlCodedString* coded_string,
                                       Walker<VisitorImpl>::Position position,
                                       OutOfLineDepth depth) {
  auto string_ptr = PtrTo<fidl_string_t>(position);
  // The MSB of the size is reserved for an ownership bit used by fidl::StringView.
  // fidl::StringView's count() would be ideally used in place of the direct bit masking
  // here, but because of build dependencies this is currently not possible.
  const uint64_t size = string_ptr->size & ~kVectorOwnershipMask;
  auto status = visitor_->VisitVectorOrStringCount(&string_ptr->size);
  FIDL_STATUS_GUARD(status);
  if (string_ptr->data == nullptr) {
    if (!coded_string->nullable && !VisitorImpl::kAllowNonNullableCollectionsToBeAbsent) {
      visitor_->OnError("non-nullable string is absent");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
    if (size == 0) {
      if (coded_string->nullable || !VisitorImpl::kAllowNonNullableCollectionsToBeAbsent) {
        return Result::kContinue;
      }
    } else {
      visitor_->OnError("string is absent but length is not zero");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
  }
  uint64_t bound = coded_string->max_size;
  if (size > std::numeric_limits<uint32_t>::max()) {
    visitor_->OnError("string size overflows 32 bits");
    FIDL_STATUS_GUARD(Status::kMemoryError);
  }
  if (size > bound) {
    visitor_->OnError("message tried to access too large of a bounded string");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }
  OutOfLineDepth array_depth = INCREASE_DEPTH(depth);
  FIDL_DEPTH_GUARD(array_depth);
  Position array_position;
  status = visitor_->VisitPointer(
      position, VisitorImpl::PointeeType::kString,
      &reinterpret_cast<Ptr<void>&>(const_cast<Ptr<char>&>(string_ptr->data)),
      static_cast<uint32_t>(size), &array_position);
  FIDL_STATUS_GUARD(status);
  return Result::kContinue;
}

template <typename VisitorImpl>
Result Walker<VisitorImpl>::WalkHandle(const FidlCodedHandle* coded_handle,
                                       Walker<VisitorImpl>::Position position) {
  auto handle_ptr = PtrTo<zx_handle_t>(position);
  if (*handle_ptr == ZX_HANDLE_INVALID) {
    if (!coded_handle->nullable) {
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

template <typename VisitorImpl>
Result Walker<VisitorImpl>::WalkVector(const FidlCodedVector* coded_vector,
                                       Walker<VisitorImpl>::Position position,
                                       OutOfLineDepth depth) {
  auto vector_ptr = PtrTo<fidl_vector_t>(position);
  // The MSB of the count is reserved for an ownership bit used by fidl::VectorView.
  // fidl::VectorView's count() would be ideally used in place of the direct bit masking
  // here, but because of build dependencies this is currently not possible.
  const uint64_t count = vector_ptr->count & ~kVectorOwnershipMask;
  auto status = visitor_->VisitVectorOrStringCount(&vector_ptr->count);
  FIDL_STATUS_GUARD(status);
  if (vector_ptr->data == nullptr) {
    if (!coded_vector->nullable && !VisitorImpl::kAllowNonNullableCollectionsToBeAbsent) {
      visitor_->OnError("non-nullable vector is absent");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
    if (count == 0) {
      if (coded_vector->nullable || !VisitorImpl::kAllowNonNullableCollectionsToBeAbsent) {
        return Result::kContinue;
      }
    } else {
      visitor_->OnError("absent vector of non-zero elements");
      FIDL_STATUS_GUARD(Status::kConstraintViolationError);
    }
  }
  if (count > coded_vector->max_count) {
    visitor_->OnError("message tried to access too large of a bounded vector");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }
  uint32_t size;
  if (mul_overflow(count, coded_vector->element_size, &size)) {
    visitor_->OnError("integer overflow calculating vector size");
    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
  }
  OutOfLineDepth array_depth = INCREASE_DEPTH(depth);
  FIDL_DEPTH_GUARD(array_depth);
  Position array_position;
  status = visitor_->VisitPointer(position, VisitorImpl::PointeeType::kVector, &vector_ptr->data,
                                  size, &array_position);
  FIDL_STATUS_GUARD(status);
  if (coded_vector->element) {
    uint32_t stride = coded_vector->element_size;
    ZX_ASSERT(count <= std::numeric_limits<uint32_t>::max());
    uint32_t end_offset = uint32_t(count) * stride;
    return WalkIterableInternal(coded_vector->element, array_position, stride, end_offset,
                                array_depth);
  }
  return Result::kContinue;
}

}  // namespace internal

// Walks the FIDL message, calling hooks in the concrete VisitorImpl.
//
// |visitor|        is an implementation of the fidl::Visitor interface.
// |type|           is the coding table for the FIDL type. It cannot be null.
// |start|          is the starting point for the walk.
template <typename VisitorImpl>
void Walk(VisitorImpl& visitor, const fidl_type_t* type, typename VisitorImpl::Position start) {
  internal::Walker<VisitorImpl> walker(&visitor);
  walker.Walk(type, start);
}

// Infer the size of the primary object, from the coding table in |type|.
// Ensures that the primary object is of one of the expected types.
//
// An error is returned if:
// - |type| is null
// - The primary object is neither a struct nor a table.
zx_status_t PrimaryObjectSize(const fidl_type_t* type, size_t* out_size, const char** out_error);

// Calculate the offset of the first out-of-line object, from the coding table in |type|.
// Ensures that the primary object is of one of the expected types, and the offset falls within the
// |buffer_size| constraints.
//
// An error is returned if:
// - |type| is null
// - The primary object is neither a struct nor a table.
// - The offset overflows, or is larger than |buffer_size|.
zx_status_t StartingOutOfLineOffset(const fidl_type_t* type, uint32_t buffer_size,
                                    uint32_t* out_first_out_of_line, const char** out_error);

}  // namespace fidl

#endif  // LIB_FIDL_WALKER_H_
