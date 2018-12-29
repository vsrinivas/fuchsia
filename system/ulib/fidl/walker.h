// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdalign.h>
#include <type_traits>

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include "visitor.h"

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

constexpr uint32_t TypeSize(const fidl_type_t* type) {
    switch (type->type_tag) {
        case fidl::kFidlTypeStructPointer:
        case fidl::kFidlTypeTablePointer:
        case fidl::kFidlTypeUnionPointer:
        case fidl::kFidlTypeXUnionPointer:
            return sizeof(uint64_t);
        case fidl::kFidlTypeHandle:
            return sizeof(zx_handle_t);
        case fidl::kFidlTypeStruct:
            return type->coded_struct.size;
        case fidl::kFidlTypeTable:
            return sizeof(fidl_vector_t);
        case fidl::kFidlTypeUnion:
            return type->coded_union.size;
        case fidl::kFidlTypeXUnion:
            return sizeof(fidl_xunion_t);
        case fidl::kFidlTypeString:
            return sizeof(fidl_string_t);
        case fidl::kFidlTypeArray:
            return type->coded_array.array_size;
        case fidl::kFidlTypeVector:
            return sizeof(fidl_vector_t);
    }
    __builtin_unreachable();
}

// The Walker class traverses through a FIDL message by following its encoding table and
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
        Frame(const fidl_type_t* fidl_type, Position position)
            : position(position) {
            switch (fidl_type->type_tag) {
                case fidl::kFidlTypeStruct:
                    state = kStateStruct;
                    struct_state.fields = fidl_type->coded_struct.fields;
                    struct_state.field_count = fidl_type->coded_struct.field_count;
                    struct_state.field = 0;
                    break;
                case fidl::kFidlTypeStructPointer:
                    state = kStateStructPointer;
                    struct_pointer_state.struct_type = fidl_type->coded_struct_pointer.struct_type;
                    break;
                case fidl::kFidlTypeTable:
                    state = kStateTable;
                    table_state.field = fidl_type->coded_table.fields;
                    table_state.remaining_fields = fidl_type->coded_table.field_count;
                    table_state.present_count = 0;
                    table_state.ordinal = 0;
                    break;
                case fidl::kFidlTypeTablePointer:
                    state = kStateTablePointer;
                    table_pointer_state.table_type = fidl_type->coded_table_pointer.table_type;
                    break;
                case fidl::kFidlTypeUnion:
                    state = kStateUnion;
                    union_state.types = fidl_type->coded_union.types;
                    union_state.type_count = fidl_type->coded_union.type_count;
                    union_state.data_offset = fidl_type->coded_union.data_offset;
                    break;
                case fidl::kFidlTypeUnionPointer:
                    state = kStateUnionPointer;
                    union_pointer_state.union_type = fidl_type->coded_union_pointer.union_type;
                    break;
                case fidl::kFidlTypeXUnion:
                    state = kStateXUnion;
                    xunion_state.fields = fidl_type->coded_xunion.fields;
                    xunion_state.field_count = fidl_type->coded_xunion.field_count;
                    break;
                case fidl::kFidlTypeXUnionPointer:
                    state = kStateXUnionPointer;
                    xunion_pointer_state.xunion_type = fidl_type->coded_xunion_pointer.xunion_type;
                    break;
                case fidl::kFidlTypeArray:
                    state = kStateArray;
                    array_state.element = fidl_type->coded_array.element;
                    array_state.array_size = fidl_type->coded_array.array_size;
                    array_state.element_size = fidl_type->coded_array.element_size;
                    array_state.element_offset = 0;
                    break;
                case fidl::kFidlTypeString:
                    state = kStateString;
                    string_state.max_size = fidl_type->coded_string.max_size;
                    string_state.nullable = fidl_type->coded_string.nullable;
                    break;
                case fidl::kFidlTypeHandle:
                    state = kStateHandle;
                    handle_state.nullable = fidl_type->coded_handle.nullable;
                    break;
                case fidl::kFidlTypeVector:
                    state = kStateVector;
                    vector_state.element = fidl_type->coded_vector.element;
                    vector_state.max_count = fidl_type->coded_vector.max_count;
                    vector_state.element_size = fidl_type->coded_vector.element_size;
                    vector_state.nullable = fidl_type->coded_vector.nullable;
                    break;
            }
        }

        Frame(const fidl::FidlCodedStruct* coded_struct, Position position)
            : position(position) {
            state = kStateStruct;
            struct_state.fields = coded_struct->fields;
            struct_state.field_count = coded_struct->field_count;
            struct_state.field = 0;
        }

        Frame(const fidl::FidlCodedTable* coded_table, Position position)
            : position(position) {
            state = kStateStruct;
            table_state.field = coded_table->fields;
            table_state.remaining_fields = coded_table->field_count;
            table_state.present_count = 0;
            table_state.ordinal = 0;
        }

        Frame(const fidl::FidlCodedUnion* coded_union, Position position)
            : position(position) {
            state = kStateUnion;
            union_state.types = coded_union->types;
            union_state.type_count = coded_union->type_count;
            union_state.data_offset = coded_union->data_offset;
        }

        Frame(const fidl::FidlCodedXUnion* coded_xunion, Position position)
            : state(kStateXUnion), position(position) {
            // This initialization is done in the ctor body instead of in an
            // initialization list since we need to set fields in unions, which
            // is much more involved in a ctor initialization list.
            xunion_state.fields = coded_xunion->fields;
            xunion_state.field_count = coded_xunion->field_count;
        }

        Frame(const fidl_type_t* element, uint32_t array_size, uint32_t element_size,
              Position position)
            : position(position) {
            state = kStateArray;
            array_state.element = element;
            array_state.array_size = array_size;
            array_state.element_size = element_size;
            array_state.element_offset = 0;
        }

        // The default constructor does nothing when initializing the stack of frames.
        Frame() {}

        static Frame DoneSentinel() {
            Frame frame;
            frame.state = kStateDone;
            return frame;
        }

        uint32_t NextStructField() {
            ZX_DEBUG_ASSERT(state == kStateStruct);

            uint32_t current = struct_state.field;
            struct_state.field += 1;
            return current;
        }

        uint32_t NextArrayOffset() {
            ZX_DEBUG_ASSERT(state == kStateArray);

            uint32_t current = array_state.element_offset;
            array_state.element_offset += array_state.element_size;
            return current;
        }

        enum : int {
            kStateStruct,
            kStateStructPointer,
            kStateTable,
            kStateTablePointer,
            kStateUnion,
            kStateUnionPointer,
            kStateXUnion,
            kStateXUnionPointer,
            kStateArray,
            kStateString,
            kStateHandle,
            kStateVector,

            kStateDone,
        } state;

        // Position into the message.
        Position position;

        // This is a subset of the information recorded in the
        // fidl_type structures needed for decoding state. For
        // example, struct sizes do not need to be present here.
        union {
            struct {
                const fidl::FidlField* fields;
                uint32_t field_count;
                // Index of the currently processing field.
                uint32_t field;
            } struct_state;
            struct {
                const fidl::FidlCodedStruct* struct_type;
            } struct_pointer_state;
            struct {
                // Sparse coding table array for fields;
                // advance the field pointer on every matched ordinal to save space
                const fidl::FidlTableField* field;
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
                const fidl::FidlCodedTable* table_type;
            } table_pointer_state;
            struct {
                // Array of coding table corresponding to each union variant.
                // The union tag counts upwards from 0 without breaks; hence it can be used to
                // index into the |types| array.
                const fidl_type_t* const* types;
                // Size of the |types| array. Equal to the number of tags.
                uint32_t type_count;
                // Offset of the payload in the wire format (size of tag + padding).
                uint32_t data_offset;
            } union_state;
            struct {
                const fidl::FidlCodedUnion* union_type;
            } union_pointer_state;
            struct {
                const fidl::FidlXUnionField* fields;
                uint32_t field_count;
            } xunion_state;
            struct {
                const fidl::FidlCodedXUnion* xunion_type;
            } xunion_pointer_state;
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
        decoding_frames_[depth_] = frame;
        ++depth_;
        return true;
    }

    void Pop() {
        ZX_DEBUG_ASSERT(depth_ != 0u);
        --depth_;
    }

    Frame* Peek() {
        ZX_DEBUG_ASSERT(depth_ != 0u);
        return &decoding_frames_[depth_ - 1];
    }

    const fidl_type_t* const type_;
    const StartingPoint start_;

    // Decoding stack state.
    uint32_t depth_ = 0u;
    Frame decoding_frames_[FIDL_RECURSION_DEPTH];
};

template <typename VisitorImpl>
void Walker<VisitorImpl>::Walk(VisitorImpl& visitor) {
    Push(Frame::DoneSentinel());
    Push(Frame(type_, start_.ToPosition()));

// Macro to insert the relevant goop required to support two control flows here in case of error:
// one where we keep reading after error, and another where we return immediately.
#define FIDL_STATUS_GUARD(status)                                       \
    switch ((status)) {                                                 \
        case Status::kSuccess:                                          \
            break;                                                      \
        case Status::kConstraintViolationError:                         \
            if (VisitorImpl::kContinueAfterConstraintViolation) {       \
                Pop();                                                  \
                continue;                                               \
            } else {                                                    \
                return;                                                 \
            }                                                           \
        case Status::kMemoryError:                                      \
            return;                                                     \
    }                                                                   \

    for (;;) {
        Frame* frame = Peek();

        switch (frame->state) {
            case Frame::kStateStruct: {
                const uint32_t field_index = frame->NextStructField();
                if (field_index == frame->struct_state.field_count) {
                    Pop();
                    continue;
                }
                const fidl::FidlField& field = frame->struct_state.fields[field_index];
                const fidl_type_t* field_type = field.type;
                Position field_position = frame->position + field.offset;
                if (!Push(Frame(field_type, field_position))) {
                    visitor.OnError("recursion depth exceeded processing struct");
                    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
                }
                continue;
            }
            case Frame::kStateStructPointer: {
                if (*PtrTo<void*>(frame->position) == nullptr) {
                    Pop();
                    continue;
                }
                auto status = visitor.VisitPointer(frame->position,
                                                   PtrTo<void*>(frame->position),
                                                   frame->struct_pointer_state.struct_type->size,
                                                   &frame->position);
                FIDL_STATUS_GUARD(status);
                const fidl::FidlCodedStruct* coded_struct = frame->struct_pointer_state.struct_type;
                *frame = Frame(coded_struct, frame->position);
                continue;
            }
            case Frame::kStateTable: {
                auto& table_frame = frame->table_state;
                // Utility to locate the position of the Nth-ordinal envelope header
                auto envelope_position = [&frame] (uint32_t ordinal) -> Position {
                    return frame->position
                        + (ordinal - 1) * static_cast<uint32_t>(sizeof(fidl_envelope_t));
                };
                if (table_frame.ordinal == 0) {
                    // Process the vector part of the table
                    auto envelope_vector_ptr = PtrTo<fidl_vector_t>(frame->position);
                    if (envelope_vector_ptr->data == nullptr) {
                        visitor.OnError("Table data cannot be absent");
                        FIDL_STATUS_GUARD(Status::kConstraintViolationError);
                    }
                    uint32_t size;
                    if (mul_overflow(envelope_vector_ptr->count, sizeof(fidl_envelope_t), &size)) {
                        visitor.OnError("integer overflow calculating table size");
                        return;
                    }
                    auto status = visitor.VisitPointer(frame->position,
                                                       &envelope_vector_ptr->data,
                                                       size,
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
                const fidl::FidlTableField* known_field = nullptr;
                if (table_frame.remaining_fields > 0) {
                    const fidl::FidlTableField* field = table_frame.field;
                    if (field->ordinal == table_frame.ordinal) {
                        known_field = field;
                        table_frame.field += 1;
                        table_frame.remaining_fields -= 1;
                    }
                }
                Position envelope_pos = envelope_position(table_frame.ordinal);
                auto envelope_ptr = PtrTo<fidl_envelope_t>(envelope_pos);
                // Process the next ordinal in the following state machine iteration
                table_frame.ordinal += 1;
                // Make sure we don't process a malformed envelope
                const fidl_type_t* payload_type = known_field ? known_field->type : nullptr;
                auto status = visitor.EnterEnvelope(envelope_pos, envelope_ptr, payload_type);
                FIDL_STATUS_GUARD(status);
                table_frame.inside_envelope = true;
                // Skip empty envelopes
                if (envelope_ptr->data == nullptr) {
                    continue;
                }
                if (known_field != nullptr) {
                    const fidl_type_t* field_type = known_field->type;
                    Position position;
                    auto status =
                        visitor.VisitPointer(frame->position,
                                             // casting since |envelope_ptr->data| is always void*
                                             &const_cast<Ptr<void>&>(envelope_ptr->data),
                                             TypeSize(field_type),
                                             &position);
                    FIDL_STATUS_GUARD(status);
                    if (!Push(Frame(field_type, position))) {
                        visitor.OnError("recursion depth exceeded processing table");
                        FIDL_STATUS_GUARD(Status::kConstraintViolationError);
                    }
                } else {
                    // No coding table for this ordinal.
                    // Still patch pointers, but cannot recurse into the payload.
                    Position position;
                    auto status =
                        visitor.VisitPointer(frame->position,
                                             &const_cast<Ptr<void>&>(envelope_ptr->data),
                                             envelope_ptr->num_bytes,
                                             &position);
                    FIDL_STATUS_GUARD(status);
                }
                continue;
            }
            case Frame::kStateTablePointer: {
                if (*PtrTo<fidl_vector_t*>(frame->position) == nullptr) {
                    Pop();
                    continue;
                }
                auto status = visitor.VisitPointer(frame->position,
                                                   PtrTo<void*>(frame->position),
                                                   static_cast<uint32_t>(sizeof(fidl_vector_t)),
                                                   &frame->position);
                FIDL_STATUS_GUARD(status);
                const fidl::FidlCodedTable* coded_table = frame->table_pointer_state.table_type;
                *frame = Frame(coded_table, frame->position);
                continue;
            }
            case Frame::kStateUnion: {
                auto union_tag = *PtrTo<fidl_union_tag_t>(frame->position);
                if (union_tag >= frame->union_state.type_count) {
                    visitor.OnError("Bad union discriminant");
                    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
                }
                const fidl_type_t* member = frame->union_state.types[union_tag];
                if (!member) {
                    Pop();
                    continue;
                }
                frame->position += frame->union_state.data_offset;
                *frame = Frame(member, frame->position);
                continue;
            }
            case Frame::kStateUnionPointer: {
                if (*PtrTo<fidl_union_tag_t*>(frame->position) == nullptr) {
                    Pop();
                    continue;
                }
                auto status = visitor.VisitPointer(frame->position,
                                                   PtrTo<void*>(frame->position),
                                                   frame->union_pointer_state.union_type->size,
                                                   &frame->position);
                FIDL_STATUS_GUARD(status);
                const fidl::FidlCodedUnion* coded_union = frame->union_pointer_state.union_type;
                *frame = Frame(coded_union, frame->position);
                continue;
            }
            case Frame::kStateXUnion: {
                assert(false && "xunions not implemented yet");
                continue;
            }
            case Frame::kStateXUnionPointer: {
                assert(false && "xunion pointers not implemented yet");
                continue;
            }
            case Frame::kStateArray: {
                const uint32_t element_offset = frame->NextArrayOffset();
                if (element_offset == frame->array_state.array_size) {
                    Pop();
                    continue;
                }
                const fidl_type_t* element_type = frame->array_state.element;
                Position position = frame->position + element_offset;
                if (!Push(Frame(element_type, position))) {
                    visitor.OnError("recursion depth exceeded processing array");
                    FIDL_STATUS_GUARD(Status::kConstraintViolationError);
                }
                continue;
            }
            case Frame::kStateString: {
                auto string_ptr = PtrTo<fidl_string_t>(frame->position);
                if (string_ptr->data == nullptr) {
                    if (!frame->string_state.nullable) {
                        visitor.OnError("non-nullable string is absent");
                        FIDL_STATUS_GUARD(Status::kConstraintViolationError);
                    }
                    if (string_ptr->size != 0) {
                        visitor.OnError("string is absent but length is not zero");
                        FIDL_STATUS_GUARD(Status::kConstraintViolationError);
                    }
                    Pop();
                    continue;
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
                auto status = visitor.VisitPointer(position,
                                                   &const_cast<Ptr<void>&>(
                                                       reinterpret_cast<void*&>(string_ptr->data)),
                                                   static_cast<uint32_t>(size),
                                                   &position);
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
                    if (!frame->vector_state.nullable) {
                        visitor.OnError("non-nullable vector is absent");
                        FIDL_STATUS_GUARD(Status::kConstraintViolationError);
                    }
                    if (vector_ptr->count != 0) {
                        visitor.OnError("absent vector of non-zero elements");
                        FIDL_STATUS_GUARD(Status::kConstraintViolationError);
                    }
                    Pop();
                    continue;
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
                auto status = visitor.VisitPointer(frame->position,
                                                   &vector_ptr->data,
                                                   size,
                                                   &frame->position);
                FIDL_STATUS_GUARD(status);
                if (frame->vector_state.element) {
                    // Continue by decoding the vector elements as an array.
                    *frame = Frame(frame->vector_state.element, size,
                                   frame->vector_state.element_size, frame->position);
                } else {
                    // If there is no element type pointer, there is
                    // nothing to process in the vector secondary
                    // payload. So just continue.
                    Pop();
                }
                continue;
            }
            case Frame::kStateDone: {
                return;
            }
        }
    }
}

} // namespace internal

// Walks the FIDL message, calling hooks in the concrete VisitorImpl.
//
// |visitor|        is an implementation of the fidl::Visitor interface.
// |type|           is the coding table for the FIDL type. It cannot be null.
// |start|          is the starting point for the walk.
template <typename VisitorImpl>
void Walk(VisitorImpl& visitor,
          const fidl_type_t* type,
          typename VisitorImpl::StartingPoint start) {
    internal::Walker<VisitorImpl> walker(type, start);
    walker.Walk(visitor);
}

// Given a FIDL coding table, first ensure that the primary object is of one of the expected types,
// then return the size of the primary object.
//
// Currently the primary object must be either a struct or a table. An error is returned if this is
// not the case.
zx_status_t GetPrimaryObjectSize(const fidl_type_t* type,
                                 size_t* out_size,
                                 const char** out_error);

} // namespace fidl
