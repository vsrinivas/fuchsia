// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace fidl {
namespace internal {

// Some assumptions about data type layout.
static_assert(offsetof(fidl_string_t, size) == 0u, "");
static_assert(offsetof(fidl_string_t, data) == 8u, "");

static_assert(offsetof(fidl_vector_t, count) == 0u, "");
static_assert(offsetof(fidl_vector_t, data) == 8u, "");

static_assert(ZX_HANDLE_INVALID == FIDL_HANDLE_ABSENT, "");

template <bool kConst, class U>
struct SetPtrConst;

template <class U>
struct SetPtrConst<false, U> {
    typedef U* type;
};

template <class U>
struct SetPtrConst<true, U> {
    typedef const U* type;
};

// Walks over a FIDL buffer and validates/encodes/decodes it per-Derived.
//
// kMutating controls whether this deals with mutable bytes or immutable bytes
// (validation wants immutable, encode/decode wants mutable)
//
// kContinueAfterErrors controls whether parsing is continued upon failure (encode needs this to
// see all available handles).
//
// Derived should offer the following methods:
//
//   const? uint8_t* bytes() - returns the start of the buffer of bytes
//   uint32_t num_bytes() - returns the number of bytes in said buffer
//   uint32_t num_handles() - returns the number of handles that are claimable
//   bool ValidateOutOfLineStorageClaim(const void* a, const void* b)
//      - returns true if a legally points to b
//   void UnclaimedHandle(zx_handle_t*) - notes that a handle was skipped
//   void ClaimedHandle(zx_handle_t*, uint32_t idx) - notes that a handle was claimed
//   PointerState GetPointerState(const void* ptr) - returns whether a pointer is present or not
//   HandleState GetHandleState(zx_handle_t) - returns if a handle is present or not
//   void UpdatePointer(T**p, T*v) - mutates a pointer representation for a present pointer
//   void SetError(const char* error_msg) - flags that an error occurred
template <class Derived, bool kMutating, bool kContinueAfterErrors>
class BufferWalker {
public:
    explicit BufferWalker(const fidl_type* type)
        : type_(type) {}

    void Walk() {
        // The first decode is special. It must be a struct or a table.
        // We need to know the size of the first element to compute the start of
        // the out-of-line allocations.

        if (type_ == nullptr) {
            SetError("Cannot decode a null fidl type");
            return;
        }

        if (bytes() == nullptr) {
            SetError("Cannot decode null bytes");
            return;
        }

        switch (type_->type_tag) {
        case fidl::kFidlTypeStruct:
            if (num_bytes() < type_->coded_struct.size) {
                SetError("Message size is smaller than expected");
                return;
            }
            out_of_line_offset_ = static_cast<uint32_t>(fidl::FidlAlign(type_->coded_struct.size));
            break;
        case fidl::kFidlTypeTable:
            if (num_bytes() < sizeof(fidl_vector_t)) {
                SetError("Message size is smaller than expected");
                return;
            }
            out_of_line_offset_ = static_cast<uint32_t>(fidl::FidlAlign(sizeof(fidl_vector_t)));
            break;
        default:
            SetError("Message must be a struct or a table");
            return;
        }

        Push(Frame::DoneSentinel());
        Push(Frame(type_, 0u));

// Macro to insert the relevant goop required to support two control flows here:
// one where we keep reading after error, and another where we return immediately.
// No runtime overhead thanks to if constexpr magic.
#define FIDL_POP_AND_CONTINUE_OR_RETURN \
    if (kContinueAfterErrors) {         \
        Pop();                          \
        continue;                       \
    } else {                            \
        return;                         \
    }

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
                const uint32_t field_offset = frame->offset + field.offset;
                if (!Push(Frame(field_type, field_offset))) {
                    SetError("recursion depth exceeded processing struct");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                continue;
            }
            case Frame::kStateStructPointer: {
                switch (GetPointerState(TypedAt<void>(frame->offset))) {
                case PointerState::PRESENT:
                    break;
                case PointerState::ABSENT:
                    Pop();
                    continue;
                default:
                    SetError("Tried to decode a bad struct pointer");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                auto struct_ptr_ptr = TypedAt<void*>(frame->offset);
                if (!ClaimOutOfLineStorage(frame->struct_pointer_state.struct_type->size,
                                           *struct_ptr_ptr, &frame->offset)) {
                    SetError("message wanted to store too large of a nullable struct");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                UpdatePointer(struct_ptr_ptr, TypedAt<void>(frame->offset));
                const fidl::FidlCodedStruct* coded_struct = frame->struct_pointer_state.struct_type;
                *frame = Frame(coded_struct, frame->offset);
                continue;
            }
            case Frame::kStateTable: {
                if (frame->field == 0u) {
                    auto envelope_vector_ptr = TypedAt<fidl_vector_t>(frame->offset);
                    switch (GetPointerState(&envelope_vector_ptr->data)) {
                    case PointerState::PRESENT:
                        break;
                    case PointerState::ABSENT:
                        SetError("Table data cannot be absent");
                        FIDL_POP_AND_CONTINUE_OR_RETURN;
                    default:
                        SetError("message tried to decode a non-present vector");
                        FIDL_POP_AND_CONTINUE_OR_RETURN;
                    }
                    uint32_t size;
                    if (mul_overflow(envelope_vector_ptr->count, 2 * sizeof(uint64_t), &size)) {
                        SetError("integer overflow calculating table size");
                        FIDL_POP_AND_CONTINUE_OR_RETURN;
                    }
                    if (!ClaimOutOfLineStorage(size, envelope_vector_ptr->data, &frame->offset)) {
                        SetError("message wanted to store too large of a table");
                        FIDL_POP_AND_CONTINUE_OR_RETURN;
                    }
                    UpdatePointer(&envelope_vector_ptr->data, TypedAt<void>(frame->offset));
                    frame->field = 1;
                    frame->table_state.known_index = 0;
                    frame->table_state.present_count = static_cast<uint32_t>(envelope_vector_ptr->count);
                    frame->table_state.end_offset = out_of_line_offset_;
                    frame->table_state.end_handle = handle_idx_;
                    continue;
                }
                if (frame->table_state.end_offset != out_of_line_offset_) {
                    SetError("Table field was mis-sized");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                if (frame->table_state.end_handle != handle_idx_) {
                    SetError("Table handles were mis-sized");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                if (frame->field > frame->table_state.present_count) {
                    Pop();
                    continue;
                }
                const fidl::FidlTableField* known_field = nullptr;
                if (frame->table_state.known_index < frame->table_state.field_count) {
                    const fidl::FidlTableField* field =
                        &frame->table_state.fields[frame->table_state.known_index];
                    if (field->ordinal == frame->field) {
                        known_field = field;
                        frame->table_state.known_index++;
                    }
                }
                const uint32_t tag_offset = static_cast<uint32_t>(
                    frame->offset + (frame->field - 1) * 2 * sizeof(uint64_t));
                const uint32_t data_offset = static_cast<uint32_t>(
                    tag_offset + sizeof(uint64_t));
                const uint64_t packed_sizes = *TypedAt<uint64_t>(tag_offset);
                frame->field++;
                switch (GetPointerState(TypedAt<void>(data_offset))) {
                case PointerState::PRESENT:
                    if (packed_sizes != 0)
                        break; // expected

                    SetError("Table envelope has present data pointer, but no data, and no handles");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                case PointerState::ABSENT:
                    if (packed_sizes == 0)
                        continue; // skip

                    SetError("Table envelope has absent data pointer, yet has data and/or handles");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                default:
                    SetError("Table envelope has bad data pointer");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                uint32_t offset;
                uint32_t handles;
                const uint32_t table_bytes = static_cast<uint32_t>(packed_sizes & 0xffffffffu);
                const uint32_t table_handles = static_cast<uint32_t>(packed_sizes >> 32);
                if (add_overflow(out_of_line_offset_, table_bytes, &offset) || offset > num_bytes()) {
                    SetError("integer overflow decoding table field");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                if (add_overflow(handle_idx_, table_handles, &handles) ||
                    handles > num_handles()) {
                    SetError("integer overflow decoding table handles");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                frame->table_state.end_offset = offset;
                frame->table_state.end_handle = handles;
                if (known_field != nullptr) {
                    const fidl_type_t* field_type = known_field->type;
                    uint32_t field_offset;
                    if (!ClaimOutOfLineStorage(TypeSize(field_type), TypedAt<void*>(data_offset), &field_offset)) {
                        SetError("table wanted too many bytes in field");
                        FIDL_POP_AND_CONTINUE_OR_RETURN;
                    }
                    UpdatePointer(TypedAt<void*>(data_offset), TypedAt<void>(field_offset));
                    if (!Push(Frame(field_type, field_offset))) {
                        SetError("recursion depth exceeded decoding table");
                        FIDL_POP_AND_CONTINUE_OR_RETURN;
                    }
                } else {
                    // Table data will not be processed: discard it.
                    uint32_t field_offset;
                    if (!ClaimOutOfLineStorage(table_bytes, TypedAt<void*>(data_offset), &field_offset)) {
                        SetError("table wanted too many bytes in field");
                        FIDL_POP_AND_CONTINUE_OR_RETURN;
                    }
                    UpdatePointer(TypedAt<void*>(data_offset), TypedAt<void>(field_offset));
                    for (uint32_t i = 0; i < table_handles; i++) {
                        if (!ClaimHandle(nullptr)) {
                            SetError("expected handle not present");
                            FIDL_POP_AND_CONTINUE_OR_RETURN;
                        }
                    }
                }
                continue;
            }
            case Frame::kStateTablePointer: {
                switch (GetPointerState(TypedAt<void>(frame->offset))) {
                case PointerState::PRESENT:
                    break;
                case PointerState::ABSENT:
                    Pop();
                    continue;
                default:
                    SetError("Tried to decode a bad table pointer");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                auto table_ptr_ptr = TypedAt<void*>(frame->offset);
                if (!ClaimOutOfLineStorage(sizeof(fidl_vector_t), *table_ptr_ptr, &frame->offset)) {
                    SetError("message wanted to store too large of a nullable table");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                UpdatePointer(table_ptr_ptr, TypedAt<void>(frame->offset));
                const fidl::FidlCodedTable* coded_table = frame->table_pointer_state.table_type;
                *frame = Frame(coded_table, frame->offset);
                continue;
            }
            case Frame::kStateUnion: {
                fidl_union_tag_t union_tag = *TypedAt<fidl_union_tag_t>(frame->offset);
                if (union_tag >= frame->union_state.type_count) {
                    SetError("Tried to decode a bad union discriminant");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                const fidl_type_t* member = frame->union_state.types[union_tag];
                if (!member) {
                    Pop();
                    continue;
                }
                frame->offset += frame->union_state.data_offset;
                *frame = Frame(member, frame->offset);
                continue;
            }
            case Frame::kStateUnionPointer: {
                auto union_ptr_ptr = TypedAt<fidl_union_tag_t*>(frame->offset);
                switch (GetPointerState(TypedAt<void>(frame->offset))) {
                case PointerState::PRESENT:
                    break;
                case PointerState::ABSENT:
                    Pop();
                    continue;
                default:
                    SetError("Tried to decode a bad union pointer");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                if (!ClaimOutOfLineStorage(frame->union_pointer_state.union_type->size, *union_ptr_ptr,
                                           &frame->offset)) {
                    SetError("message wanted to store too large of a nullable union");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                UpdatePointer(union_ptr_ptr, TypedAt<fidl_union_tag_t>(frame->offset));
                const fidl::FidlCodedUnion* coded_union = frame->union_pointer_state.union_type;
                *frame = Frame(coded_union, frame->offset);
                continue;
            }
            case Frame::kStateArray: {
                const uint32_t element_offset = frame->NextArrayOffset();
                if (element_offset == frame->array_state.array_size) {
                    Pop();
                    continue;
                }
                const fidl_type_t* element_type = frame->array_state.element;
                const uint32_t offset = frame->offset + element_offset;
                if (!Push(Frame(element_type, offset))) {
                    SetError("recursion depth exceeded decoding array");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                continue;
            }
            case Frame::kStateString: {
                auto string_ptr = TypedAt<fidl_string_t>(frame->offset);
                // The string storage may be Absent for nullable strings and must
                // otherwise be Present. No other values are allowed.
                switch (GetPointerState(&string_ptr->data)) {
                case PointerState::PRESENT:
                    break;
                case PointerState::ABSENT:
                    if (!frame->string_state.nullable) {
                        SetError("message tried to decode an absent non-nullable string");
                        FIDL_POP_AND_CONTINUE_OR_RETURN;
                    }
                    if (string_ptr->size != 0u) {
                        SetError("message tried to decode an absent string of non-zero length");
                        FIDL_POP_AND_CONTINUE_OR_RETURN;
                    }
                    Pop();
                    continue;
                default:
                    SetError(
                        "message tried to decode a string that is neither present nor absent");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                uint64_t bound = frame->string_state.max_size;
                uint64_t size = string_ptr->size;
                if (size > bound) {
                    SetError("message tried to decode too large of a bounded string");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                uint32_t string_data_offset = 0u;
                if (!ClaimOutOfLineStorage(static_cast<uint32_t>(size), string_ptr->data, &string_data_offset)) {
                    SetError("decoding a string overflowed buffer");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                UpdatePointer(&string_ptr->data, TypedAt<char>(string_data_offset));
                Pop();
                continue;
            }
            case Frame::kStateHandle: {
                auto handle_ptr = TypedAt<zx_handle_t>(frame->offset);
                // The handle storage may be Absent for nullable handles and must
                // otherwise be Present. No other values are allowed.
                switch (GetHandleState(*handle_ptr)) {
                case HandleState::ABSENT:
                    if (frame->handle_state.nullable) {
                        Pop();
                        continue;
                    }
                    SetError("message tried to decode a non-present handle");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                case HandleState::PRESENT:
                    if (!ClaimHandle(handle_ptr)) {
                        SetError("message decoded too many handles");
                        FIDL_POP_AND_CONTINUE_OR_RETURN;
                    }
                    Pop();
                    continue;
                default:
                    // The value at the handle was garbage.
                    SetError("message tried to decode a garbage handle");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
            }
            case Frame::kStateVector: {
                auto vector_ptr = TypedAt<fidl_vector_t>(frame->offset);
                // The vector storage may be Absent for nullable vectors and must
                // otherwise be Present. No other values are allowed.
                switch (GetPointerState(&vector_ptr->data)) {
                case PointerState::PRESENT:
                    break;
                case PointerState::ABSENT:
                    if (!frame->vector_state.nullable) {
                        SetError("message tried to decode an absent non-nullable vector");
                        FIDL_POP_AND_CONTINUE_OR_RETURN;
                    }
                    if (vector_ptr->count != 0u) {
                        SetError("message tried to decode an absent vector of non-zero elements");
                        FIDL_POP_AND_CONTINUE_OR_RETURN;
                    }
                    Pop();
                    continue;
                default:
                    SetError("message tried to decode a non-present vector");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                if (vector_ptr->count > frame->vector_state.max_count) {
                    SetError("message tried to decode too large of a bounded vector");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                uint32_t size;
                if (mul_overflow(vector_ptr->count, frame->vector_state.element_size, &size)) {
                    SetError("integer overflow calculating vector size");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                if (!ClaimOutOfLineStorage(size, vector_ptr->data, &frame->offset)) {
                    SetError("message wanted to store too large of a vector");
                    FIDL_POP_AND_CONTINUE_OR_RETURN;
                }
                UpdatePointer(&vector_ptr->data, TypedAt<void>(frame->offset));
                if (frame->vector_state.element) {
                    // Continue by decoding the vector elements as an array.
                    *frame = Frame(frame->vector_state.element, size,
                                   frame->vector_state.element_size, frame->offset);
                } else {
                    // If there is no element type pointer, there is
                    // nothing to decode in the vector secondary
                    // payload. So just continue.
                    Pop();
                }
                continue;
            }
            case Frame::kStateDone: {
                if (out_of_line_offset_ != num_bytes()) {
                    SetError("message did not decode all provided bytes");
                }
                return;
            }
            }
        }

#undef FIDL_POP_AND_CONTINUE_OR_RETURN
    }

protected:
    void SetError(const char* error_msg) {
        derived()->SetError(error_msg);
    }

    template <typename T>
    typename SetPtrConst<!kMutating, T>::type TypedAt(uint32_t offset) const {
        return reinterpret_cast<typename SetPtrConst<!kMutating, T>::type>(bytes() + offset);
    }

    enum class PointerState : uintptr_t {
        PRESENT = FIDL_ALLOC_PRESENT,
        ABSENT = FIDL_ALLOC_ABSENT,
        INVALID = 1 // *OR* *ANY* non PRESENT/ABSENT value.
    };

    enum class HandleState : zx_handle_t {
        PRESENT = FIDL_HANDLE_PRESENT,
        ABSENT = FIDL_HANDLE_ABSENT,
        INVALID = 1 // *OR* *ANY* non PRESENT/ABSENT value.
    };

    uint32_t handle_idx() const { return handle_idx_; }

private:
    Derived* derived() {
        return static_cast<Derived*>(this);
    }

    const Derived* derived() const {
        return static_cast<const Derived*>(this);
    }

    // Returns a pointer to the bytes in the message.
    auto bytes() const {
        return derived()->bytes();
    }

    // Returns the number of bytes in the message.
    auto num_bytes() const {
        return derived()->num_bytes();
    }

    // Returns the number of handles in the message (encoding: the max number of handles in the message).
    auto num_handles() const {
        return derived()->num_handles();
    }

    // Returns PRESENT/ABSENT/INVALID for a given pointer value.
    PointerState GetPointerState(const void* ptr) const {
        return derived()->GetPointerState(ptr);
    }

    // Returns PRESENT/ABSENT/INVALID for a given handle value.
    HandleState GetHandleState(zx_handle_t p) const {
        return derived()->GetHandleState(p);
    }

    // If required: mutate a pointer to the dual representation.
    template <class T2, class T1>
    void UpdatePointer(T2 p, T1 v) {
        derived()->UpdatePointer(p, v);
    }

    // Returns true when a handle was claimed, and false when the
    // handles are exhausted.
    template <class ZxHandleTPointer>
    bool ClaimHandle(ZxHandleTPointer out_handle) {
        if (handle_idx_ == num_handles()) {
            derived()->UnclaimedHandle(out_handle);
            return false;
        }
        derived()->ClaimedHandle(out_handle, handle_idx_);
        ++handle_idx_;
        return true;
    }

    // Returns true when the buffer space is claimed, and false when
    // the requested claim is too large for bytes_.
    bool ClaimOutOfLineStorage(uint32_t size, const void* storage, uint32_t* out_offset) {
        if (!derived()->ValidateOutOfLineStorageClaim(storage, &bytes()[out_of_line_offset_])) {
            return false;
        }

        // We have to manually maintain alignment here. For example, a pointer
        // to a struct that is 4 bytes still needs to advance the next
        // out-of-line offset by 8 to maintain the aligned-to-FIDL_ALIGNMENT
        // property.
        static constexpr uint32_t mask = FIDL_ALIGNMENT - 1;
        uint32_t offset = out_of_line_offset_;
        if (add_overflow(offset, size, &offset) ||
            add_overflow(offset, mask, &offset)) {
            return false;
        }
        offset &= ~mask;

        if (offset > num_bytes()) {
            return false;
        }
        *out_offset = out_of_line_offset_;
        out_of_line_offset_ = offset;
        return true;
    }

    uint32_t TypeSize(const fidl_type_t* type) {
        switch (type->type_tag) {
        case fidl::kFidlTypeStructPointer:
        case fidl::kFidlTypeTablePointer:
        case fidl::kFidlTypeUnionPointer:
            return sizeof(uint64_t);
        case fidl::kFidlTypeHandle:
            return sizeof(zx_handle_t);
        case fidl::kFidlTypeStruct:
            return type->coded_struct.size;
        case fidl::kFidlTypeTable:
            return sizeof(fidl_vector_t);
        case fidl::kFidlTypeUnion:
            return type->coded_union.size;
        case fidl::kFidlTypeString:
            return sizeof(fidl_string_t);
        case fidl::kFidlTypeArray:
            return type->coded_array.array_size;
        case fidl::kFidlTypeVector:
            return sizeof(fidl_vector_t);
        }
        abort();
        return 0;
    }

    // Functions that manipulate the decoding stack frames.
    struct Frame {
        Frame(const fidl_type_t* fidl_type, uint32_t offset)
            : offset(offset) {
            switch (fidl_type->type_tag) {
            case fidl::kFidlTypeStruct:
                state = kStateStruct;
                struct_state.fields = fidl_type->coded_struct.fields;
                struct_state.field_count = fidl_type->coded_struct.field_count;
                break;
            case fidl::kFidlTypeStructPointer:
                state = kStateStructPointer;
                struct_pointer_state.struct_type = fidl_type->coded_struct_pointer.struct_type;
                break;
            case fidl::kFidlTypeTable:
                state = kStateTable;
                table_state.fields = fidl_type->coded_table.fields;
                table_state.field_count = fidl_type->coded_table.field_count;
                table_state.present_count = 0;
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
            case fidl::kFidlTypeArray:
                state = kStateArray;
                array_state.element = fidl_type->coded_array.element;
                array_state.array_size = fidl_type->coded_array.array_size;
                array_state.element_size = fidl_type->coded_array.element_size;
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

        Frame(const fidl::FidlCodedStruct* coded_struct, uint32_t offset)
            : offset(offset) {
            state = kStateStruct;
            struct_state.fields = coded_struct->fields;
            struct_state.field_count = coded_struct->field_count;
        }

        Frame(const fidl::FidlCodedTable* coded_table, uint32_t offset)
            : offset(offset) {
            state = kStateStruct;
            table_state.fields = coded_table->fields;
            table_state.field_count = coded_table->field_count;
        }

        Frame(const fidl::FidlCodedUnion* coded_union, uint32_t offset)
            : offset(offset) {
            state = kStateUnion;
            union_state.types = coded_union->types;
            union_state.type_count = coded_union->type_count;
            union_state.data_offset = coded_union->data_offset;
        }

        Frame(const fidl_type_t* element, uint32_t array_size, uint32_t element_size,
              uint32_t offset)
            : offset(offset) {
            state = kStateArray;
            array_state.element = element;
            array_state.array_size = array_size;
            array_state.element_size = element_size;
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

            uint32_t current = field;
            field += 1;
            return current;
        }

        uint32_t NextArrayOffset() {
            ZX_DEBUG_ASSERT(state == kStateArray);

            uint32_t current = field;
            field += array_state.element_size;
            return current;
        }

        enum : int {
            kStateStruct,
            kStateStructPointer,
            kStateTable,
            kStateTablePointer,
            kStateUnion,
            kStateUnionPointer,
            kStateArray,
            kStateString,
            kStateHandle,
            kStateVector,

            kStateDone,
        } state;
        // A byte offset into bytes_;
        uint32_t offset;

        // This is a subset of the information recorded in the
        // fidl_type structures needed for decoding state. For
        // example, struct sizes do not need to be present here.
        union {
            struct {
                const fidl::FidlField* fields;
                uint32_t field_count;
            } struct_state;
            struct {
                const fidl::FidlCodedStruct* struct_type;
            } struct_pointer_state;
            struct {
                const fidl::FidlTableField* fields;
                uint32_t known_index;
                uint32_t field_count;
                uint32_t present_count;
                uint32_t end_offset;
                uint32_t end_handle;
            } table_state;
            struct {
                const fidl::FidlCodedTable* table_type;
            } table_pointer_state;
            struct {
                const fidl_type_t* const* types;
                uint32_t type_count;
                uint32_t data_offset;
            } union_state;
            struct {
                const fidl::FidlCodedUnion* union_type;
            } union_pointer_state;
            struct {
                const fidl_type_t* element;
                uint32_t array_size;
                uint32_t element_size;
            } array_state;
            struct {
                uint32_t max_size;
                bool nullable;
            } string_state;
            struct {
                bool nullable;
            } handle_state;
            struct {
                const fidl_type* element;
                uint32_t max_count;
                uint32_t element_size;
                bool nullable;
            } vector_state;
        };

        uint32_t field = 0u;
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

    // Message state passed in to the constructor.
    const fidl_type_t* const type_;

    // Internal state.
    uint32_t handle_idx_ = 0u;
    uint32_t out_of_line_offset_ = 0u;

    // Decoding stack state.
    uint32_t depth_ = 0u;
    Frame decoding_frames_[FIDL_RECURSION_DEPTH];
};

} // namespace internal
} // namespace fidl
