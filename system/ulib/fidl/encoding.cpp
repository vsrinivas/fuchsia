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

class FidlEncoder {
public:
    FidlEncoder(const fidl_type_t* type, void* bytes, uint32_t num_bytes, zx_handle_t* handles,
                uint32_t max_handles, uint32_t* actual_handles_out, const char** error_msg_out)
        : type_(type), bytes_(static_cast<uint8_t*>(bytes)), num_bytes_(num_bytes),
          handles_(handles), max_handles_(max_handles), actual_handles_out_(actual_handles_out),
          error_msg_out_(error_msg_out) {}

    zx_status_t EncodeMessage();

private:
    void SetError(const char* error_msg) {
        // If status has already been set to an error, then we don't want to clobber error_msg_out_.
        // We report the first error encountered.
        if (status_ != ZX_OK) {
            return;
        }
        status_ = ZX_ERR_INVALID_ARGS;
        if (error_msg_out_ != nullptr) {
            *error_msg_out_ = error_msg;
        }
        if (handles_ != nullptr) {
            // Return value intentionally ignored: this is best-effort cleanup.
            zx_handle_close_many(handles_, handle_idx_);
        }
    }

    template <typename T> T* TypedAt(uint32_t offset) const {
        return reinterpret_cast<T*>(bytes_ + offset);
    }

    // Returns true when a handle was claimed, and false when the
    // handles are exhausted.
    // If status_ != ZX_OK or handles are exhausted, will attempt to close *out_handle.
    bool ClaimHandle(zx_handle_t* out_handle) {
        if (handle_idx_ == max_handles_) {
            // Return value intentionally ignored: this is best-effort cleanup.
            zx_handle_close(*out_handle);
            return false;
        }
        if (status_ == ZX_OK) {
            handles_[handle_idx_] = *out_handle;
            *out_handle = FIDL_HANDLE_PRESENT;
        } else {
            // We've already encountered an error earlier, just clean up.
            // Return value intentionally ignored: this is best-effort cleanup.
            zx_handle_close(*out_handle);
        }
        ++handle_idx_;
        return true;
    }

    // Returns true when the buffer space is claimed, and false when
    // the requested claim is too large for bytes_.
    bool ClaimOutOfLineStorage(uint32_t size, const void* storage, uint32_t* out_offset) {
        static constexpr uint32_t mask = FIDL_ALIGNMENT - 1;

        // We have to manually maintain alignment here. For example, a pointer
        // to a struct that is 4 bytes still needs to advance the next
        // out-of-line offset by 8 to maintain the aligned-to-FIDL_ALIGNMENT
        // property.
        if (&bytes_[out_of_line_offset_] != static_cast<const uint8_t*>(storage)) {
            return false;
        }

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
        // fidl_type structures needed for encoding state. For
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
        encoding_frames_[depth_] = frame;
        ++depth_;
        return true;
    }

    void Pop() {
        ZX_DEBUG_ASSERT(depth_ != 0u);
        --depth_;
    }

    Frame* Peek() {
        ZX_DEBUG_ASSERT(depth_ != 0u);
        return &encoding_frames_[depth_ - 1];
    }

    // Message state passed in to the constructor.
    const fidl_type_t* const type_;
    uint8_t* const bytes_;
    const uint32_t num_bytes_;
    zx_handle_t* const handles_;
    const uint32_t max_handles_;
    uint32_t* const actual_handles_out_;
    const char** error_msg_out_;

    // Internal state.
    uint32_t handle_idx_ = 0u;
    uint32_t out_of_line_offset_ = 0u;
    zx_status_t status_ = ZX_OK;

    // Encoding stack state.
    uint32_t depth_ = 0u;
    Frame encoding_frames_[FIDL_RECURSION_DEPTH];
};

zx_status_t FidlEncoder::EncodeMessage() {
    // The first encode is special. It must be a struct. We need to
    // know the size of the struct to compute the start of the
    // out-of-line allocations.

    if (type_ == nullptr) {
        SetError("Cannot encode a null fidl type");
        return status_;
    }

    if (bytes_ == nullptr) {
        SetError("Cannot encode null bytes");
        return status_;
    }

    if (actual_handles_out_ == nullptr) {
        SetError("Cannot encode with null actual_handles_out");
        return status_;
    }

    if (handles_ == nullptr && max_handles_ != 0u) {
        SetError("Cannot provide non-zero handle count and null handle pointer");
        return status_;
    }

    if (type_->type_tag != fidl::kFidlTypeStruct) {
        SetError("Message must be a struct");
        return status_;
    }

    if (type_->coded_struct.size > num_bytes_) {
        SetError("num_bytes is less than required according to type");
        return status_;
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
                SetError("recursion depth exceeded encoding struct");
                Pop();
            }
            continue;
        }
        case Frame::kStateStructPointer: {
            void** struct_ptr_ptr = TypedAt<void*>(frame->offset);
            if (*struct_ptr_ptr == nullptr) {
                Pop();
                continue;
            }
            if (!ClaimOutOfLineStorage(frame->struct_pointer_state.struct_type->size,
                                       *struct_ptr_ptr, &frame->offset)) {
                SetError("message wanted to store too large of a nullable struct");
                Pop();
                continue;
            }
            *struct_ptr_ptr = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
            const fidl::FidlCodedStruct* coded_struct = frame->struct_pointer_state.struct_type;
            // Continue to the struct case.
            *frame = Frame(coded_struct, frame->offset);
            continue;
        }
        case Frame::kStateUnion: {
            fidl_union_tag_t union_tag = *TypedAt<fidl_union_tag_t>(frame->offset);
            if (union_tag >= frame->union_state.type_count) {
                SetError("Tried to encode a bad union discriminant");
                Pop();
                continue;
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
            if (*union_ptr_ptr == nullptr) {
                Pop();
                continue;
            }
            if (!ClaimOutOfLineStorage(frame->union_pointer_state.union_type->size, *union_ptr_ptr,
                                       &frame->offset)) {
                SetError("message wanted to store too large of a nullable union");
                Pop();
                continue;
            }
            *union_ptr_ptr = reinterpret_cast<fidl_union_tag_t*>(FIDL_ALLOC_PRESENT);
            const fidl::FidlCodedUnion* coded_union = frame->union_pointer_state.union_type;
            // Continue to the union case.
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
                SetError("recursion depth exceeded encoding array");
                Pop();
            }
            continue;
        }
        case Frame::kStateString: {
            fidl_string_t* string_ptr = TypedAt<fidl_string_t>(frame->offset);
            // The string storage may be nullptr for nullable strings.
            if (string_ptr->data == nullptr) {
                if (!frame->string_state.nullable) {
                    SetError("message tried to encode an absent non-nullable string");
                }
                Pop();
                continue;
            }
            uint64_t bound = frame->string_state.max_size;
            uint64_t size = string_ptr->size;
            if (size > bound) {
                SetError("message tried to encode too large of a bounded string");
                Pop();
                continue;
            }
            if (!ClaimOutOfLineStorage(static_cast<uint32_t>(size), string_ptr->data,
                                       &frame->offset)) {
                SetError("encoding a string with incorrectly placed data");
                Pop();
                continue;
            }
            string_ptr->data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
            Pop();
            continue;
        }
        case Frame::kStateHandle: {
            zx_handle_t* handle_ptr = TypedAt<zx_handle_t>(frame->offset);
            // The handle storage may be ZX_HANDLE_INVALID for
            // nullable handles, which will be encoded as
            // FIDL_HANDLE_ABSENT. All other values will be encoded as
            // FIDL_HANDLE_PRESENT.
            if (frame->handle_state.nullable && *handle_ptr == ZX_HANDLE_INVALID) {
                Pop();
                continue;
            }
            if (!ClaimHandle(handle_ptr)) {
                SetError("message encoded too many handles");
            }
            Pop();
            continue;
        }
        case Frame::kStateVector: {
            fidl_vector_t* vector_ptr = TypedAt<fidl_vector_t>(frame->offset);
            // The vector storage may be nullptr for nullable vectors.
            if (vector_ptr->data == nullptr) {
                if (!frame->vector_state.nullable) {
                    SetError("message tried to encode an absent non-nullable vector");
                }
                Pop();
                continue;
            }
            if (vector_ptr->count > frame->vector_state.max_count) {
                SetError("message tried to encode too large of a bounded vector");
                Pop();
                continue;
            }
            uint32_t size;
            if (mul_overflow(vector_ptr->count, frame->vector_state.element_size, &size)) {
                SetError("integer overflow calculating vector size");
                Pop();
                continue;
            }
            if (!ClaimOutOfLineStorage(size, vector_ptr->data, &frame->offset)) {
                SetError("message wanted to store too large of a vector");
                Pop();
                continue;
            }
            vector_ptr->data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
            if (frame->vector_state.element) {
                // Continue to encoding the vector elements as an array.
                *frame = Frame(frame->vector_state.element, size,
                               frame->vector_state.element_size, frame->offset);
            } else {
                // If there is no element type pointer, there is
                // nothing to encode in the vector secondary
                // payload. So just continue.
                Pop();
            }
            continue;
        }
        case Frame::kStateDone: {
            if (out_of_line_offset_ != num_bytes_) {
              SetError("did not encode the entire provided buffer");
            }
            if (status_ == ZX_OK) {
                *actual_handles_out_ = handle_idx_;
            }
            return status_;
        }
        }
    }
}

} // namespace

zx_status_t fidl_encode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        zx_handle_t* handles, uint32_t max_handles, uint32_t* actual_handles_out,
                        const char** error_msg_out) {
    FidlEncoder encoder(type, bytes, num_bytes, handles, max_handles, actual_handles_out,
                        error_msg_out);
    return encoder.EncodeMessage();
}
