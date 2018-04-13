// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>

#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>

#include <lib/fidl/internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>

// TODO(kulakowski) Design zx_status_t error values.

namespace {

// Some assumptions about data type layout.
static_assert(offsetof(fidl_string_t, size) == 0u, "");
static_assert(offsetof(fidl_string_t, data) == 8u, "");

static_assert(offsetof(fidl_vector_t, count) == 0u, "");
static_assert(offsetof(fidl_vector_t, data) == 8u, "");

class FidlDecoder {
public:
    FidlDecoder(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                const zx_handle_t* handles, uint32_t num_handles, const char** error_msg_out)
        : type_(type), bytes_(static_cast<uint8_t*>(bytes)), num_bytes_(num_bytes),
          handles_(handles), num_handles_(num_handles), error_msg_out_(error_msg_out) {}

    zx_status_t DecodeMessage();

private:
    zx_status_t WithError(const char* error_msg) {
        if (error_msg_out_ != nullptr) {
            *error_msg_out_ = error_msg;
        }
        if (handles_) {
            for (uint32_t i = 0; i < num_handles_; ++i) {
                // Return value intentionally ignored: this is best-effort cleanup.
                zx_handle_close(handles_[i]);
            }
        }
        return ZX_ERR_INVALID_ARGS;
    }

    template <typename T> T* TypedAt(uint32_t offset) const {
        return reinterpret_cast<T*>(bytes_ + offset);
    }

    // Returns true when a handle was claimed, and false when the
    // handles are exhausted.
    bool ClaimHandle(zx_handle_t* out_handle) {
        if (handle_idx_ == num_handles_) {
            return false;
        }
        *out_handle = handles_[handle_idx_];
        ++handle_idx_;
        return true;
    }

    // Returns true when the buffer space is claimed, and false when
    // the requested claim is too large for bytes_.
    bool ClaimOutOfLineStorage(uint32_t size, uint32_t* out_offset) {
        static constexpr uint32_t mask = FIDL_ALIGNMENT - 1;

        // We have to manually maintain alignment here. For example, a pointer
        // to a struct that is 4 bytes still needs to advance the next
        // out-of-line offset by 8 to maintain the aligned-to-FIDL_ALIGNMENT
        // property.
        uint32_t offset = out_of_line_offset_;
        if (add_overflow(offset, size, &offset) ||
            add_overflow(offset, mask, &offset)) {
            return false;
        }
        offset &= ~mask;

        if (offset > num_bytes_) {
            return false;
        }
        *out_offset = out_of_line_offset_;
        out_of_line_offset_ = offset;
        return true;
    }

    // Functions that manipulate the decoding stack frames.
    struct Frame {
        Frame(const fidl_type_t* fidl_type, uint32_t offset) : offset(offset) {
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

        Frame(const fidl::FidlCodedStruct* coded_struct, uint32_t offset) : offset(offset) {
            state = kStateStruct;
            struct_state.fields = coded_struct->fields;
            struct_state.field_count = coded_struct->field_count;
        }

        Frame(const fidl::FidlCodedUnion* coded_union, uint32_t offset) : offset(offset) {
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
    uint8_t* const bytes_;
    const uint32_t num_bytes_;
    const zx_handle_t* const handles_;
    const uint32_t num_handles_;
    const char** error_msg_out_;

    // Internal state.
    uint32_t handle_idx_ = 0u;
    uint32_t out_of_line_offset_ = 0u;

    // Decoding stack state.
    uint32_t depth_ = 0u;
    Frame decoding_frames_[FIDL_RECURSION_DEPTH];
};

zx_status_t FidlDecoder::DecodeMessage() {
    // The first decode is special. It must be a struct. We need to
    // know the size of the struct to compute the start of the
    // out-of-line allocations.

    if (type_ == nullptr) {
        return WithError("Cannot decode a null fidl type");
    }

    if (bytes_ == nullptr) {
        return WithError("Cannot decode null bytes");
    }

    if (handles_ == nullptr && num_handles_ != 0u) {
        return WithError("Cannot provide non-zero handle count and null handle pointer");
    }

    if (type_->type_tag != fidl::kFidlTypeStruct) {
        return WithError("Message must be a struct");
    }

    if (type_->coded_struct.size > num_bytes_) {
        return WithError("Message size is smaller than expected");
    }

    out_of_line_offset_ = static_cast<uint32_t>(fidl::FidlAlign(type_->coded_struct.size));

    Push(Frame::DoneSentinel());
    Push(Frame(type_, 0u));

    for (;;) {
        Frame* frame = Peek();

        switch (frame->state) {
        case Frame::kStateStruct: {
            uint32_t field_index = frame->NextStructField();
            if (field_index == frame->struct_state.field_count) {
                Pop();
                continue;
            }
            const fidl::FidlField& field = frame->struct_state.fields[field_index];
            const fidl_type_t* field_type = field.type;
            uint32_t field_offset = frame->offset + field.offset;
            if (!Push(Frame(field_type, field_offset))) {
                return WithError("recursion depth exceeded decoding struct");
            }
            continue;
        }
        case Frame::kStateStructPointer: {
            switch (*TypedAt<uintptr_t>(frame->offset)) {
            case FIDL_ALLOC_PRESENT:
                break;
            case FIDL_ALLOC_ABSENT:
                Pop();
                continue;
            default:
                return WithError("Tried to decode a bad struct pointer");
            }
            void** struct_ptr_ptr = TypedAt<void*>(frame->offset);
            if (!ClaimOutOfLineStorage(frame->struct_pointer_state.struct_type->size,
                                       &frame->offset)) {
                return WithError("message wanted to store too large of a nullable struct");
            }
            *struct_ptr_ptr = TypedAt<void>(frame->offset);
            const fidl::FidlCodedStruct* coded_struct = frame->struct_pointer_state.struct_type;
            *frame = Frame(coded_struct, frame->offset);
            continue;
        }
        case Frame::kStateUnion: {
            fidl_union_tag_t union_tag = *TypedAt<fidl_union_tag_t>(frame->offset);
            if (union_tag >= frame->union_state.type_count) {
                return WithError("Tried to decode a bad union discriminant");
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
            fidl_union_tag_t** union_ptr_ptr = TypedAt<fidl_union_tag_t*>(frame->offset);
            switch (*TypedAt<uintptr_t>(frame->offset)) {
            case FIDL_ALLOC_PRESENT:
                break;
            case FIDL_ALLOC_ABSENT:
                Pop();
                continue;
            default:
                return WithError("Tried to decode a bad union pointer");
            }
            if (!ClaimOutOfLineStorage(frame->union_pointer_state.union_type->size,
                                       &frame->offset)) {
                return WithError("message wanted to store too large of a nullable union");
            }
            *union_ptr_ptr = TypedAt<fidl_union_tag_t>(frame->offset);
            const fidl::FidlCodedUnion* coded_union = frame->union_pointer_state.union_type;
            *frame = Frame(coded_union, frame->offset);
            continue;
        }
        case Frame::kStateArray: {
            uint32_t element_offset = frame->NextArrayOffset();
            if (element_offset == frame->array_state.array_size) {
                Pop();
                continue;
            }
            const fidl_type_t* element_type = frame->array_state.element;
            uint32_t offset = frame->offset + element_offset;
            if (!Push(Frame(element_type, offset))) {
                return WithError("recursion depth exceeded decoding array");
            }
            continue;
        }
        case Frame::kStateString: {
            fidl_string_t* string_ptr = TypedAt<fidl_string_t>(frame->offset);
            // The string storage may be Absent for nullable strings and must
            // otherwise be Present. No other values are allowed.
            switch (reinterpret_cast<uintptr_t>(string_ptr->data)) {
            case FIDL_ALLOC_PRESENT:
                break;
            case FIDL_ALLOC_ABSENT:
                if (!frame->string_state.nullable) {
                    return WithError("message tried to decode an absent non-nullable string");
                }
                if (string_ptr->size != 0u) {
                    return WithError("message tried to decode an absent string of non-zero length");
                }
                Pop();
                continue;
            default:
                return WithError(
                    "message tried to decode a string that is neither present nor absent");
            }
            uint64_t bound = frame->string_state.max_size;
            uint64_t size = string_ptr->size;
            if (size > bound) {
                return WithError("message tried to decode too large of a bounded string");
            }
            uint32_t string_data_offset = 0u;
            if (!ClaimOutOfLineStorage(static_cast<uint32_t>(size), &string_data_offset)) {
                return WithError("decoding a  string overflowed buffer");
            }
            string_ptr->data = TypedAt<char>(string_data_offset);
            Pop();
            continue;
        }
        case Frame::kStateHandle: {
            zx_handle_t* handle_ptr = TypedAt<zx_handle_t>(frame->offset);
            // The handle storage may be Absent for nullable handles and must
            // otherwise be Present. No other values are allowed.
            switch (*handle_ptr) {
            case FIDL_HANDLE_ABSENT:
                if (frame->handle_state.nullable) {
                    Pop();
                    continue;
                }
                break;
            case FIDL_HANDLE_PRESENT:
                if (!ClaimHandle(handle_ptr)) {
                    return WithError("message decoded too many handles");
                }
                Pop();
                continue;
            }
            // Either the value at the handle was garbage, or was
            // ABSENT for a nonnullable handle.
            return WithError("message tried to decode a non-present handle");
        }
        case Frame::kStateVector: {
            fidl_vector_t* vector_ptr = TypedAt<fidl_vector_t>(frame->offset);
            // The vector storage may be Absent for nullable vectors and must
            // otherwise be Present. No other values are allowed.
            switch (reinterpret_cast<uintptr_t>(vector_ptr->data)) {
            case FIDL_ALLOC_PRESENT:
                break;
            case FIDL_ALLOC_ABSENT:
                if (!frame->vector_state.nullable) {
                    return WithError("message tried to decode an absent non-nullable vector");
                }
                if (vector_ptr->count != 0u) {
                    return WithError("message tried to decode an absent vector of non-zero elements");
                }
                Pop();
                continue;
            default:
                return WithError("message tried to decode a non-present vector");
            }
            if (vector_ptr->count > frame->vector_state.max_count) {
                return WithError("message tried to decode too large of a bounded vector");
            }
            uint32_t size;
            if (mul_overflow(vector_ptr->count, frame->vector_state.element_size, &size)) {
                return WithError("integer overflow calculating vector size");
            }
            if (!ClaimOutOfLineStorage(size, &frame->offset)) {
                return WithError("message wanted to store too large of a vector");
            }
            vector_ptr->data = TypedAt<void>(frame->offset);
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
            if (out_of_line_offset_ != num_bytes_) {
                return WithError("message did not decode all provided bytes");
            }
            if (handle_idx_ != num_handles_) {
                return WithError("message did not contain the specified number of handles");
            }
            return ZX_OK;
        }
        }
    }
}

} // namespace

zx_status_t fidl_decode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        const zx_handle_t* handles, uint32_t num_handles,
                        const char** error_msg_out) {
    FidlDecoder decoder(type, bytes, num_bytes, handles, num_handles, error_msg_out);
    return decoder.DecodeMessage();
}
