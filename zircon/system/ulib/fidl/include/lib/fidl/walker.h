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

static_assert(ZX_HANDLE_INVALID == FIDL_HANDLE_ABSENT, "invalid handle equals absence marker");

constexpr uint32_t PrimitiveSize(const FidlCodedPrimitive primitive) {
  switch (primitive) {
    case kFidlCodedPrimitive_Bool:
    case kFidlCodedPrimitive_Int8:
    case kFidlCodedPrimitive_Uint8:
      return 1;
    case kFidlCodedPrimitive_Int16:
    case kFidlCodedPrimitive_Uint16:
      return 2;
    case kFidlCodedPrimitive_Int32:
    case kFidlCodedPrimitive_Uint32:
    case kFidlCodedPrimitive_Float32:
      return 4;
    case kFidlCodedPrimitive_Int64:
    case kFidlCodedPrimitive_Uint64:
    case kFidlCodedPrimitive_Float64:
      return 8;
  }
  __builtin_unreachable();
}

constexpr uint32_t TypeSize(const fidl_type_t* type) {
  switch (type->type_tag) {
    case kFidlTypePrimitive:
      return PrimitiveSize(type->coded_primitive);
    case kFidlTypeEnum:
      return PrimitiveSize(type->coded_enum.underlying_type);
    case kFidlTypeBits:
      return PrimitiveSize(type->coded_bits.underlying_type);
    case kFidlTypeStructPointer:
    case kFidlTypeUnionPointer:
      return sizeof(uint64_t);
    case kFidlTypeHandle:
      return sizeof(zx_handle_t);
    case kFidlTypeStruct:
      return type->coded_struct.size;
    case kFidlTypeTable:
      return sizeof(fidl_vector_t);
    case kFidlTypeUnion:
      return type->coded_union.size;
    case kFidlTypeXUnion:
      return sizeof(fidl_xunion_t);
    case kFidlTypeString:
      return sizeof(fidl_string_t);
    case kFidlTypeArray:
      return type->coded_array.array_size;
    case kFidlTypeVector:
      return sizeof(fidl_vector_t);
  }
  __builtin_unreachable();
}

constexpr bool IsPrimitive(const fidl_type_t* type) {
  switch (type->type_tag) {
    case kFidlTypePrimitive:
      return true;
    default:
      return false;
  }
}

// The Walker class traverses through a FIDL message by following its coding table and
// calling the visitor implementation. VisitorImpl must be a concrete implementation of the
// fidl::Visitor interface. The concrete type is used to eliminate dynamic dispatch.
template <typename VisitorImpl>
class Walker final {
 private:
  using MutationTrait = typename VisitorImpl::MutationTrait;

  using StartingPoint = typename VisitorImpl::StartingPoint;

  using Position = typename VisitorImpl::Position;

  using VisitorSuper = Visitor<MutationTrait, StartingPoint, Position>;

  using Status = typename VisitorSuper::Status;

  static_assert(CheckVisitorInterface<VisitorSuper, VisitorImpl>(), "");

 public:
  Walker(const fidl_type_t* type, StartingPoint start) : type_(type), start_(start) {}

  // Walk the object/buffer located at |start_|.
  void Walk(VisitorImpl& visitor);

 private:
  // Optionally uses non-const pointers depending on if the visitor
  // is declared as mutating or not.
  template <typename T>
  using Ptr = typename VisitorImpl::template Ptr<T>;

  // Wrapper around Position::Get with friendlier syntax.
  template <typename T>
  Ptr<T> PtrTo(Position position) {
    return position.template Get<T>(start_);
  }

  // Functions that manipulate the coding stack frames.
  struct Frame {
    Frame(const fidl_type_t* fidl_type, Position position) : position(position) {
      switch (fidl_type->type_tag) {
        case kFidlTypeEnum:
          state = kStateEnum;
          enum_state.underlying_type = fidl_type->coded_enum.underlying_type;
          enum_state.validate = fidl_type->coded_enum.validate;
          break;
        case kFidlTypeBits:
          state = kStateBits;
          bits_state.underlying_type = fidl_type->coded_bits.underlying_type;
          bits_state.mask = fidl_type->coded_bits.mask;
          break;
        case kFidlTypeStruct:
          state = kStateStruct;
          struct_state.fields = fidl_type->coded_struct.fields;
          struct_state.field_count = fidl_type->coded_struct.field_count;
          struct_state.field = 0;
          struct_state.struct_size = fidl_type->coded_struct.size;
          break;
        case kFidlTypeStructPointer:
          state = kStateStructPointer;
          struct_pointer_state.struct_type = fidl_type->coded_struct_pointer.struct_type;
          break;
        case kFidlTypeTable:
          state = kStateTable;
          table_state.field = fidl_type->coded_table.fields;
          table_state.remaining_fields = fidl_type->coded_table.field_count;
          table_state.present_count = 0;
          table_state.ordinal = 0;
          break;
        case kFidlTypeUnion:
          state = kStateUnion;
          union_state.fields = fidl_type->coded_union.fields;
          union_state.field_count = fidl_type->coded_union.field_count;
          union_state.data_offset = fidl_type->coded_union.data_offset;
          union_state.union_size = fidl_type->coded_union.size;
          break;
        case kFidlTypeUnionPointer:
          state = kStateUnionPointer;
          union_pointer_state.union_type = fidl_type->coded_union_pointer.union_type;
          break;
        case kFidlTypeXUnion:
          state = kStateXUnion;
          xunion_state.fields = fidl_type->coded_xunion.fields;
          xunion_state.field_count = fidl_type->coded_xunion.field_count;
          xunion_state.inside_envelope = false;
          xunion_state.nullable = fidl_type->coded_xunion.nullable;
          xunion_state.strictness = fidl_type->coded_xunion.strictness;
          break;
        case kFidlTypeArray:
          state = kStateArray;
          array_state.element = fidl_type->coded_array.element;
          array_state.array_size = fidl_type->coded_array.array_size;
          array_state.element_size = fidl_type->coded_array.element_size;
          array_state.element_offset = 0;
          break;
        case kFidlTypeString:
          state = kStateString;
          string_state.max_size = fidl_type->coded_string.max_size;
          string_state.nullable = fidl_type->coded_string.nullable;
          break;
        case kFidlTypeHandle:
          state = kStateHandle;
          handle_state.nullable = fidl_type->coded_handle.nullable;
          break;
        case kFidlTypeVector:
          state = kStateVector;
          vector_state.element = fidl_type->coded_vector.element;
          vector_state.max_count = fidl_type->coded_vector.max_count;
          vector_state.element_size = fidl_type->coded_vector.element_size;
          vector_state.nullable = fidl_type->coded_vector.nullable;
          break;
        case kFidlTypePrimitive:
          state = kStatePrimitive;
          break;
      }
    }

    Frame(const FidlCodedStruct* coded_struct, Position position) : position(position) {
      state = kStateStruct;
      struct_state.fields = coded_struct->fields;
      struct_state.field_count = coded_struct->field_count;
      struct_state.field = 0;
      struct_state.struct_size = coded_struct->size;
    }

    Frame(const FidlCodedTable* coded_table, Position position) : position(position) {
      state = kStateStruct;
      table_state.field = coded_table->fields;
      table_state.remaining_fields = coded_table->field_count;
      table_state.present_count = 0;
      table_state.ordinal = 0;
    }

    Frame(const FidlCodedUnion* coded_union, Position position) : position(position) {
      state = kStateUnion;
      union_state.fields = coded_union->fields;
      union_state.field_count = coded_union->field_count;
      union_state.data_offset = coded_union->data_offset;
      union_state.union_size = coded_union->size;
    }

    Frame(const FidlCodedXUnion* coded_xunion, Position position)
        : state(kStateXUnion), position(position) {
      // This initialization is done in the ctor body instead of in an
      // initialization list since we need to set fields in unions, which
      // is much more involved in a ctor initialization list.
      xunion_state.fields = coded_xunion->fields;
      xunion_state.field_count = coded_xunion->field_count;
      xunion_state.inside_envelope = false;
      xunion_state.nullable = coded_xunion->nullable;
      xunion_state.strictness = coded_xunion->strictness;
    }

    Frame(const fidl_type_t* element, uint32_t array_size, uint32_t element_size, Position position)
        : position(position) {
      state = kStateArray;
      array_state.element = element;
      array_state.array_size = array_size;
      array_state.element_size = element_size;
      array_state.element_offset = 0;
    }

    // The default constructor does nothing when initializing the stack of frames.
    Frame() = default;

    static Frame DoneSentinel() {
      Frame frame;
      frame.state = kStateDone;
      return frame;
    }

    uint32_t NextStructField() {
      ZX_DEBUG_ASSERT(state == kStateStruct);

      uint32_t current = struct_state.field;
      struct_state.field++;
      return current;
    }

    uint32_t NextArrayOffset() {
      ZX_DEBUG_ASSERT(state == kStateArray);

      uint32_t current = array_state.element_offset;
      array_state.element_offset += array_state.element_size;
      return current;
    }

    enum : int {
      kStateEnum,
      kStateBits,
      kStateStruct,
      kStateStructPointer,
      kStateTable,
      kStateUnion,
      kStateUnionPointer,
      kStateXUnion,
      kStateArray,
      kStateString,
      kStateHandle,
      kStateVector,
      kStatePrimitive,

      kStateDone,
    } state;

    // Position into the message.
    Position position;

    // This is a subset of the information recorded in the
    // fidl_type structures needed for coding state. For
    // example, struct sizes do not need to be present here.
    union {
      struct {
        FidlCodedPrimitive underlying_type;
        EnumValidationPredicate validate;
      } enum_state;
      struct {
        FidlCodedPrimitive underlying_type;
        uint64_t mask;
      } bits_state;
      struct {
        const FidlStructField* fields;
        uint32_t field_count;
        // Index of the currently processing field.
        uint32_t field;
        // Size of the entire struct.
        uint32_t struct_size;
      } struct_state;
      struct {
        const FidlCodedStruct* struct_type;
      } struct_pointer_state;
      struct {
        // Sparse (but monotonically increasing) coding table array for fields;
        // advance the |field| pointer on every matched ordinal to save space
        const FidlTableField* field;
        // Number of unseen fields in the coding table
        uint32_t remaining_fields;
        // How many fields are stored in the message
        uint32_t present_count;
        // Current ordinal (valid ordinals start at 1)
        uint32_t ordinal;
        // When true, the walker is currently working within an envelope, or equivalently,
        // |EnterEnvelope| was successful.
        bool inside_envelope;
      } table_state;
      struct {
        // Array of coding table corresponding to each union variant.
        // The union tag counts upwards from 0 without breaks; hence it can be used to
        // index into the |fields| array.
        const FidlUnionField* fields;
        // Size of the |fields| array. Equal to the number of tags.
        uint32_t field_count;
        // Offset of the payload in the wire format (size of tag + padding).
        uint32_t data_offset;
        // Size of the entire union.
        uint32_t union_size;
      } union_state;
      struct {
        const FidlCodedUnion* union_type;
      } union_pointer_state;
      struct {
        const FidlXUnionField* fields;
        // Number of known ordinals declared in the coding table
        uint32_t field_count;
        // When true, the walker is currently working within an envelope, or equivalently,
        // |EnterEnvelope| was successful.
        bool inside_envelope;
        FidlNullability nullable;
        FidlStrictness strictness;
      } xunion_state;
      struct {
        const fidl_type_t* element;
        // Size of the entire array in bytes
        uint32_t array_size;
        // Size of a single element in bytes
        uint32_t element_size;
        // Byte offset of the current element being processed
        uint32_t element_offset;
      } array_state;
      struct {
        uint32_t max_size;
        bool nullable;
      } string_state;
      struct {
        bool nullable;
      } handle_state;
      struct {
        const fidl_type_t* element;
        // Upperbound on number of elements.
        uint32_t max_count;
        // Size of a single element in bytes
        uint32_t element_size;
        bool nullable;
      } vector_state;
    };
  };

  // Returns true on success and false on recursion overflow.
  bool Push(Frame frame) {
    if (depth_ == FIDL_RECURSION_DEPTH) {
      return false;
    }
    coding_frames_[depth_] = frame;
    ++depth_;
    return true;
  }

  void Pop() {
    ZX_DEBUG_ASSERT(depth_ != 0u);
    --depth_;
  }

  Frame* Peek() {
    ZX_DEBUG_ASSERT(depth_ != 0u);
    return &coding_frames_[depth_ - 1];
  }

  const fidl_type_t* const type_;
  const StartingPoint start_;

  // Decoding stack state.
  uint32_t depth_ = 0u;
  Frame coding_frames_[FIDL_RECURSION_DEPTH];
};

template <typename VisitorImpl>
void Walker<VisitorImpl>::Walk(VisitorImpl& visitor) {
  Push(Frame::DoneSentinel());
  Push(Frame(type_, start_.ToPosition()));

// Macro to insert the relevant goop required to support two control flows here in case of error:
// one where we keep reading after error, and another where we return immediately.
#define FIDL_STATUS_GUARD_IMPL(status, pop)                 \
  switch ((status)) {                                       \
    case Status::kSuccess:                                  \
      break;                                                \
    case Status::kConstraintViolationError:                 \
      if (VisitorImpl::kContinueAfterConstraintViolation) { \
        if ((pop)) {                                        \
          Pop();                                            \
        }                                                   \
        continue;                                           \
      } else {                                              \
        return;                                             \
      }                                                     \
    case Status::kMemoryError:                              \
      return;                                               \
  }

#define FIDL_STATUS_GUARD(status) FIDL_STATUS_GUARD_IMPL(status, true)
#define FIDL_STATUS_GUARD_NO_POP(status) FIDL_STATUS_GUARD_IMPL(status, false)

  for (;;) {
    Frame* frame = Peek();

    switch (frame->state) {
      case Frame::kStateEnum: {
        uint64_t value;
        switch (frame->enum_state.underlying_type) {
          case kFidlCodedPrimitive_Uint8:
            value = *PtrTo<uint8_t>(frame->position);
            break;
          case kFidlCodedPrimitive_Uint16:
            value = *PtrTo<uint16_t>(frame->position);
            break;
          case kFidlCodedPrimitive_Uint32:
            value = *PtrTo<uint32_t>(frame->position);
            break;
          case kFidlCodedPrimitive_Uint64:
            value = *PtrTo<uint64_t>(frame->position);
            break;
          case kFidlCodedPrimitive_Int8:
            value = static_cast<uint64_t>(*PtrTo<int8_t>(frame->position));
            break;
          case kFidlCodedPrimitive_Int16:
            value = static_cast<uint64_t>(*PtrTo<int16_t>(frame->position));
            break;
          case kFidlCodedPrimitive_Int32:
            value = static_cast<uint64_t>(*PtrTo<int32_t>(frame->position));
            break;
          case kFidlCodedPrimitive_Int64:
            value = static_cast<uint64_t>(*PtrTo<int64_t>(frame->position));
            break;
          default:
            __builtin_unreachable();
        }
        if (!frame->enum_state.validate(value)) {
          // TODO(FIDL-523): Make this strictness dependent.
          visitor.OnError("not a valid enum member");
          FIDL_STATUS_GUARD(Status::kConstraintViolationError);
        }
        Pop();
        continue;
      }
      case Frame::kStateBits: {
        uint64_t value;
        switch (frame->bits_state.underlying_type) {
          case kFidlCodedPrimitive_Uint8:
            value = *PtrTo<uint8_t>(frame->position);
            break;
          case kFidlCodedPrimitive_Uint16:
            value = *PtrTo<uint16_t>(frame->position);
            break;
          case kFidlCodedPrimitive_Uint32:
            value = *PtrTo<uint32_t>(frame->position);
            break;
          case kFidlCodedPrimitive_Uint64:
            value = *PtrTo<uint64_t>(frame->position);
            break;
          default:
            __builtin_unreachable();
        }
        if (value & ~frame->bits_state.mask) {
          visitor.OnError("not a valid bits member");
          FIDL_STATUS_GUARD(Status::kConstraintViolationError);
        }
        Pop();
        continue;
      }
      case Frame::kStateStruct: {
        const uint32_t field_index = frame->NextStructField();
        if (field_index == frame->struct_state.field_count) {
          Pop();
          continue;
        }
        const FidlStructField& field = frame->struct_state.fields[field_index];
        const fidl_type_t* field_type = field.type;
        Position field_position = frame->position + field.offset;
        if (field.padding > 0) {
          Position padding_position;
          if (field_type) {
            padding_position = field_position + TypeSize(field_type);
          } else {
            // Current type does not have coding information. |field.offset| stores the
            // offset of the padding.
            padding_position = field_position;
          }
          auto status = visitor.VisitInternalPadding(padding_position, field.padding);
          FIDL_STATUS_GUARD(status);
        }
        if (!field_type) {
          // Skip fields that do not contain codable types.
          // Such fields only serve to provide padding information.
          continue;
        }
        if (!Push(Frame(field_type, field_position))) {
          visitor.OnError("recursion depth exceeded processing struct");
          FIDL_STATUS_GUARD(Status::kConstraintViolationError);
        }
        continue;
      }
      case Frame::kStateStructPointer: {
        if (*PtrTo<Ptr<void>>(frame->position) == nullptr) {
          Pop();
          continue;
        }
        auto status =
            visitor.VisitPointer(frame->position, PtrTo<Ptr<void>>(frame->position),
                                 frame->struct_pointer_state.struct_type->size, &frame->position);
        FIDL_STATUS_GUARD(status);
        const FidlCodedStruct* coded_struct = frame->struct_pointer_state.struct_type;
        *frame = Frame(coded_struct, frame->position);
        continue;
      }
      case Frame::kStateTable: {
        auto& table_frame = frame->table_state;
        // Utility to locate the position of the Nth-ordinal envelope header
        auto envelope_position = [&frame](uint32_t ordinal) -> Position {
          return frame->position + (ordinal - 1) * static_cast<uint32_t>(sizeof(fidl_envelope_t));
        };
        if (table_frame.ordinal == 0) {
          // Process the vector part of the table
          auto envelope_vector_ptr = PtrTo<fidl_vector_t>(frame->position);
          if (envelope_vector_ptr->data == nullptr) {
            // The vector of envelope headers in a table is always non-nullable.
            if (!VisitorImpl::kAllowNonNullableCollectionsToBeAbsent) {
              visitor.OnError("Table data cannot be absent");
              FIDL_STATUS_GUARD(Status::kConstraintViolationError);
            }
            if (envelope_vector_ptr->count != 0) {
              visitor.OnError("Table envelope vector data absent but non-zero count");
              FIDL_STATUS_GUARD(Status::kConstraintViolationError);
            }
          }
          uint32_t size;
          if (mul_overflow(envelope_vector_ptr->count, sizeof(fidl_envelope_t), &size)) {
            visitor.OnError("integer overflow calculating table size");
            return;
          }
          auto status = visitor.VisitPointer(frame->position, &envelope_vector_ptr->data, size,
                                             &frame->position);
          FIDL_STATUS_GUARD(status);
          table_frame.ordinal = 1;
          table_frame.present_count = static_cast<uint32_t>(envelope_vector_ptr->count);
          table_frame.inside_envelope = false;
          continue;
        }
        if (table_frame.inside_envelope) {
          // Leave the envelope that was entered during the last iteration
          uint32_t last_ordinal = table_frame.ordinal - 1;
          ZX_DEBUG_ASSERT(last_ordinal >= 1);
          Position envelope_pos = envelope_position(last_ordinal);
          auto envelope_ptr = PtrTo<fidl_envelope_t>(envelope_pos);
          table_frame.inside_envelope = false;
          auto status = visitor.LeaveEnvelope(envelope_pos, envelope_ptr);
          FIDL_STATUS_GUARD(status);
        }
        if (table_frame.ordinal > table_frame.present_count) {
          // Processed last stored field in table. Done with this table.
          Pop();
          continue;
        }
        const FidlTableField* known_field = nullptr;
        if (table_frame.remaining_fields > 0) {
          const FidlTableField* field = table_frame.field;
          if (field->ordinal == table_frame.ordinal) {
            known_field = field;
            table_frame.field++;
            table_frame.remaining_fields--;
          }
        }
        Position envelope_pos = envelope_position(table_frame.ordinal);
        auto envelope_ptr = PtrTo<fidl_envelope_t>(envelope_pos);
        // Process the next ordinal in the following state machine iteration
        table_frame.ordinal++;
        // Make sure we don't process a malformed envelope
        const fidl_type_t* payload_type = known_field ? known_field->type : nullptr;
        auto status = visitor.EnterEnvelope(envelope_pos, envelope_ptr, payload_type);
        FIDL_STATUS_GUARD(status);
        table_frame.inside_envelope = true;
        // Skip empty envelopes
        if (envelope_ptr->data == nullptr) {
          continue;
        }
        uint32_t num_bytes =
            payload_type != nullptr ? TypeSize(payload_type) : envelope_ptr->num_bytes;
        Position position;
        status =
            visitor.VisitPointer(frame->position,
                                 // casting since |envelope_ptr->data| is always void*
                                 &const_cast<Ptr<void>&>(envelope_ptr->data), num_bytes, &position);
        // Do not pop the table frame, to guarantee calling |LeaveEnvelope|
        FIDL_STATUS_GUARD_NO_POP(status);
        if (payload_type != nullptr && !IsPrimitive(payload_type)) {
          if (!Push(Frame(payload_type, position))) {
            visitor.OnError("recursion depth exceeded processing table");
            FIDL_STATUS_GUARD_NO_POP(Status::kConstraintViolationError);
          }
        }
        continue;
      }
      case Frame::kStateUnion: {
        auto union_tag = *PtrTo<fidl_union_tag_t>(frame->position);
        if (union_tag >= frame->union_state.field_count) {
          visitor.OnError("Bad union discriminant");
          FIDL_STATUS_GUARD(Status::kConstraintViolationError);
        }
        auto variant = frame->union_state.fields[union_tag];
        if (variant.padding > 0) {
          Position padding_position =
              frame->position + (frame->union_state.union_size - variant.padding);
          auto status = visitor.VisitInternalPadding(padding_position, variant.padding);
          FIDL_STATUS_GUARD(status);
        }
        auto data_offset = frame->union_state.data_offset;
        ZX_DEBUG_ASSERT(data_offset == 4 || data_offset == 8);
        if (data_offset == 8) {
          // There is an additional 4 byte of padding after the tag.
          auto status = visitor.VisitInternalPadding(frame->position + 4, 4);
          FIDL_STATUS_GUARD(status);
        }
        const fidl_type_t* member = variant.type;
        if (!member) {
          Pop();
          continue;
        }
        frame->position += data_offset;
        *frame = Frame(member, frame->position);
        continue;
      }
      case Frame::kStateUnionPointer: {
        if (*PtrTo<Ptr<fidl_union_tag_t>>(frame->position) == nullptr) {
          Pop();
          continue;
        }
        auto status =
            visitor.VisitPointer(frame->position, PtrTo<Ptr<void>>(frame->position),
                                 frame->union_pointer_state.union_type->size, &frame->position);
        FIDL_STATUS_GUARD(status);
        const FidlCodedUnion* coded_union = frame->union_pointer_state.union_type;
        *frame = Frame(coded_union, frame->position);
        continue;
      }
      case Frame::kStateXUnion: {
        auto xunion = PtrTo<fidl_xunion_t>(frame->position);
        const auto envelope_pos = frame->position + offsetof(fidl_xunion_t, envelope);
        auto envelope_ptr = &xunion->envelope;
        // |inside_envelope| is always false when first encountering an xunion.
        if (frame->xunion_state.inside_envelope) {
          // Finished processing the xunion field, and is in clean-up state
          auto status = visitor.LeaveEnvelope(envelope_pos, envelope_ptr);
          FIDL_STATUS_GUARD(status);
          Pop();
          continue;
        }
        // Validate zero-ordinal invariants
        if (xunion->tag == 0) {
          if (envelope_ptr->data != nullptr || envelope_ptr->num_bytes != 0 ||
              envelope_ptr->num_handles != 0) {
            visitor.OnError("xunion with zero as ordinal must be empty");
            FIDL_STATUS_GUARD(Status::kConstraintViolationError);
          }
          if (!frame->xunion_state.nullable) {
            visitor.OnError("non-nullable xunion is absent");
            FIDL_STATUS_GUARD(Status::kConstraintViolationError);
          }
          Pop();
          continue;
        }
        // Find coding table corresponding to the ordinal via linear search
        const FidlXUnionField* known_field = nullptr;
        for (size_t i = 0; i < frame->xunion_state.field_count; i++) {
          const auto field = frame->xunion_state.fields + i;
          if (field->hashed_ordinal == xunion->tag || field->explicit_ordinal == xunion->tag) {
            known_field = field;
            break;
          }
        }

        if (!known_field && frame->xunion_state.strictness == kFidlStrictness_Strict) {
          visitor.OnError("strict xunion has unknown ordinal");
          FIDL_STATUS_GUARD(Status::kConstraintViolationError);
        }

        // Make sure we don't process a malformed envelope
        const fidl_type_t* payload_type = known_field ? known_field->type : nullptr;
        auto status = visitor.EnterEnvelope(envelope_pos, envelope_ptr, payload_type);
        FIDL_STATUS_GUARD(status);
        frame->xunion_state.inside_envelope = true;
        // Skip empty envelopes
        if (envelope_ptr->data == nullptr) {
          if (xunion->tag != 0) {
            visitor.OnError("empty xunion must have zero as ordinal");
            FIDL_STATUS_GUARD_NO_POP(Status::kConstraintViolationError);
          }
          continue;
        }
        uint32_t num_bytes =
            payload_type != nullptr ? TypeSize(payload_type) : envelope_ptr->num_bytes;
        Position position;
        status = visitor.VisitPointer(frame->position, &const_cast<Ptr<void>&>(envelope_ptr->data),
                                      num_bytes, &position);
        FIDL_STATUS_GUARD_NO_POP(status);
        if (payload_type != nullptr && !IsPrimitive(payload_type)) {
          if (!Push(Frame(payload_type, position))) {
            visitor.OnError("recursion depth exceeded processing xunion");
            FIDL_STATUS_GUARD_NO_POP(Status::kConstraintViolationError);
          }
        }
        continue;
      }
      case Frame::kStateArray: {
        const uint32_t element_offset = frame->NextArrayOffset();
        if (element_offset == frame->array_state.array_size) {
          Pop();
          continue;
        }
        const fidl_type_t* element_type = frame->array_state.element;
        if (element_type) {
          Position position = frame->position + element_offset;
          if (!Push(Frame(element_type, position))) {
            visitor.OnError("recursion depth exceeded processing array");
            FIDL_STATUS_GUARD(Status::kConstraintViolationError);
          }
        } else {
          // If there is no element type pointer, the array contents
          // do not need extra processing, but the array coding table
          // is present to provide size information when linearizing
          // envelopes. Just continue.
          Pop();
        }
        continue;
      }
      case Frame::kStateString: {
        auto string_ptr = PtrTo<fidl_string_t>(frame->position);
        if (string_ptr->data == nullptr) {
          if (!frame->string_state.nullable &&
              !VisitorImpl::kAllowNonNullableCollectionsToBeAbsent) {
            visitor.OnError("non-nullable string is absent");
            FIDL_STATUS_GUARD(Status::kConstraintViolationError);
          }
          if (string_ptr->size == 0) {
            if (frame->string_state.nullable ||
                !VisitorImpl::kAllowNonNullableCollectionsToBeAbsent) {
              Pop();
              continue;
            }
          } else {
            visitor.OnError("string is absent but length is not zero");
            FIDL_STATUS_GUARD(Status::kConstraintViolationError);
          }
        }
        uint64_t bound = frame->string_state.max_size;
        uint64_t size = string_ptr->size;
        if (size > std::numeric_limits<uint32_t>::max()) {
          visitor.OnError("string size overflows 32 bits");
          FIDL_STATUS_GUARD(Status::kMemoryError);
        }
        if (size > bound) {
          visitor.OnError("message tried to access too large of a bounded string");
          FIDL_STATUS_GUARD(Status::kConstraintViolationError);
        }
        Position position;
        auto status = visitor.VisitPointer(
            position, &reinterpret_cast<Ptr<void>&>(const_cast<Ptr<char>&>(string_ptr->data)),
            static_cast<uint32_t>(size), &position);
        FIDL_STATUS_GUARD(status);
        Pop();
        continue;
      }
      case Frame::kStateHandle: {
        auto handle_ptr = PtrTo<zx_handle_t>(frame->position);
        if (*handle_ptr == ZX_HANDLE_INVALID) {
          if (!frame->handle_state.nullable) {
            visitor.OnError("message is missing a non-nullable handle");
            FIDL_STATUS_GUARD(Status::kConstraintViolationError);
          }
          Pop();
          continue;
        }
        auto status = visitor.VisitHandle(frame->position, handle_ptr);
        FIDL_STATUS_GUARD(status);
        Pop();
        continue;
      }
      case Frame::kStateVector: {
        auto vector_ptr = PtrTo<fidl_vector_t>(frame->position);
        if (vector_ptr->data == nullptr) {
          if (!frame->vector_state.nullable &&
              !VisitorImpl::kAllowNonNullableCollectionsToBeAbsent) {
            visitor.OnError("non-nullable vector is absent");
            FIDL_STATUS_GUARD(Status::kConstraintViolationError);
          }
          if (vector_ptr->count == 0) {
            if (frame->vector_state.nullable ||
                !VisitorImpl::kAllowNonNullableCollectionsToBeAbsent) {
              Pop();
              continue;
            }
          } else {
            visitor.OnError("absent vector of non-zero elements");
            FIDL_STATUS_GUARD(Status::kConstraintViolationError);
          }
        }
        if (vector_ptr->count > frame->vector_state.max_count) {
          visitor.OnError("message tried to access too large of a bounded vector");
          FIDL_STATUS_GUARD(Status::kConstraintViolationError);
        }
        uint32_t size;
        if (mul_overflow(vector_ptr->count, frame->vector_state.element_size, &size)) {
          visitor.OnError("integer overflow calculating vector size");
          return;
        }
        auto status =
            visitor.VisitPointer(frame->position, &vector_ptr->data, size, &frame->position);
        FIDL_STATUS_GUARD(status);
        if (frame->vector_state.element) {
          // Continue by visiting the vector elements as an array.
          *frame = Frame(frame->vector_state.element, size, frame->vector_state.element_size,
                         frame->position);
        } else {
          // If there is no element type pointer, there is
          // nothing to process in the vector secondary
          // payload. So just continue.
          Pop();
        }
        continue;
      }
      case Frame::kStatePrimitive: {
        // Nothing to do for primitives.
        Pop();
        continue;
      }
      case Frame::kStateDone: {
        return;
      }
    }
  }
}

}  // namespace internal

// Walks the FIDL message, calling hooks in the concrete VisitorImpl.
//
// |visitor|        is an implementation of the fidl::Visitor interface.
// |type|           is the coding table for the FIDL type. It cannot be null.
// |start|          is the starting point for the walk.
template <typename VisitorImpl>
void Walk(VisitorImpl& visitor, const fidl_type_t* type,
          typename VisitorImpl::StartingPoint start) {
  internal::Walker<VisitorImpl> walker(type, start);
  walker.Walk(visitor);
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
