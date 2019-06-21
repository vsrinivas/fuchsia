// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>

#include <inttypes.h>
#include <stdarg.h>
#include <string.h>

#include <lib/fidl/internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

namespace {

class StringBuilder {
public:
    StringBuilder(char* buffer, size_t capacity)
        : buffer_(buffer), capacity_(capacity) {}

    size_t length() const { return length_; }

    void Append(const char* data, size_t length) {
        size_t remaining = capacity_ - length_;
        if (length > remaining) {
            length = remaining;
        }
        memcpy(buffer_ + length_, data, length);
        length_ += length;
    }

    void Append(const char* data) {
        Append(data, strlen(data));
    }

    void AppendPrintf(const char* format, ...) __PRINTFLIKE(2, 3) {
        va_list ap;
        va_start(ap, format);
        AppendVPrintf(format, ap);
        va_end(ap);
    }

    void AppendVPrintf(const char* format, va_list ap) {
        size_t remaining = capacity_ - length_;
        if (remaining == 0u) {
            return;
        }
        int count = vsnprintf(buffer_ + length_, remaining, format, ap);
        if (count <= 0) {
            return;
        }
        size_t length = static_cast<size_t>(count);
        length_ += (length >= remaining ? remaining : length);
    }

private:
    char* buffer_;
    size_t capacity_;
    size_t length_ = 0u;
};

void FormatNullability(StringBuilder* str, fidl::FidlNullability nullable) {
    if (nullable == fidl::kNullable) {
        str->Append("?");
    }
}

void FormatStructName(StringBuilder* str, const fidl::FidlCodedStruct* coded_struct) {
    if (coded_struct->name) {
        str->Append(coded_struct->name);
    } else {
        str->Append("struct");
    }
}

void FormatUnionName(StringBuilder* str, const fidl::FidlCodedUnion* coded_union) {
    if (coded_union->name) {
        str->Append(coded_union->name);
    } else {
        str->Append("union");
    }
}

void FormatTypeName(StringBuilder* str, const fidl_type_t* type);
void FormatElementName(StringBuilder* str, const fidl_type_t* type) {
    if (type) {
        FormatTypeName(str, type);
    } else {
        // TODO(jeffbrown): Print the actual primitive type name, assuming we
        // start recording that information in the tables.
        str->Append("primitive");
    }
}

void FormatTypeName(StringBuilder* str, const fidl_type_t* type) {
    switch (type->type_tag) {
    case fidl::kFidlTypeStruct:
        FormatStructName(str, &type->coded_struct);
        break;
    case fidl::kFidlTypeStructPointer:
        FormatStructName(str, type->coded_struct_pointer.struct_type);
        str->Append("?");
        break;
    case fidl::kFidlTypeUnion:
        FormatUnionName(str, &type->coded_union);
        break;
    case fidl::kFidlTypeUnionPointer:
        FormatUnionName(str, type->coded_union_pointer.union_type);
        str->Append("?");
        break;
    case fidl::kFidlTypeArray:
        str->Append("array<");
        FormatElementName(str, type->coded_array.element);
        str->Append(">");
        str->AppendPrintf(":%" PRIu32, type->coded_array.array_size /
                                           type->coded_array.element_size);
        break;
    case fidl::kFidlTypeString:
        str->Append("string");
        if (type->coded_string.max_size != FIDL_MAX_SIZE) {
            str->AppendPrintf(":%" PRIu32, type->coded_string.max_size);
        }
        FormatNullability(str, type->coded_string.nullable);
        break;
    case fidl::kFidlTypeHandle:
        str->Append("handle");
        if (type->coded_handle.handle_subtype) {
            str->Append("<");
            switch (type->coded_handle.handle_subtype) {
            case fidl::kFidlHandleSubtypeHandle:
                str->Append("handle");
                break;
            case fidl::kFidlHandleSubtypeProcess:
                str->Append("process");
                break;
            case fidl::kFidlHandleSubtypeThread:
                str->Append("thread");
                break;
            case fidl::kFidlHandleSubtypeVmo:
                str->Append("vmo");
                break;
            case fidl::kFidlHandleSubtypeChannel:
                str->Append("channel");
                break;
            case fidl::kFidlHandleSubtypeEvent:
                str->Append("event");
                break;
            case fidl::kFidlHandleSubtypePort:
                str->Append("port");
                break;
            case fidl::kFidlHandleSubtypeInterrupt:
                str->Append("interrupt");
                break;
            case fidl::kFidlHandleSubtypeLog:
                str->Append("log");
                break;
            case fidl::kFidlHandleSubtypeSocket:
                str->Append("socket");
                break;
            case fidl::kFidlHandleSubtypeResource:
                str->Append("resource");
                break;
            case fidl::kFidlHandleSubtypeEventpair:
                str->Append("eventpair");
                break;
            case fidl::kFidlHandleSubtypeJob:
                str->Append("job");
                break;
            case fidl::kFidlHandleSubtypeVmar:
                str->Append("vmar");
                break;
            case fidl::kFidlHandleSubtypeFifo:
                str->Append("fifo");
                break;
            case fidl::kFidlHandleSubtypeGuest:
                str->Append("guest");
                break;
            case fidl::kFidlHandleSubtypeTimer:
                str->Append("timer");
                break;
            // TODO(pascallouis): Add support for iomap, pci, and hypervisor
            // when they are supported in FIDL.
            default:
                str->AppendPrintf("%" PRIu32, type->coded_handle.handle_subtype);
                break;
            }
            str->Append(">");
        }
        FormatNullability(str, type->coded_handle.nullable);
        break;
    case fidl::kFidlTypeVector:
        str->Append("vector<");
        FormatElementName(str, type->coded_vector.element);
        str->Append(">");
        if (type->coded_vector.max_count != FIDL_MAX_SIZE) {
            str->AppendPrintf(":%" PRIu32, type->coded_vector.max_count);
        }
        FormatNullability(str, type->coded_vector.nullable);
        break;
    default:
        ZX_PANIC("unrecognized tag");
        break;
    }
}

} // namespace

size_t fidl_format_type_name(const fidl_type_t* type,
                             char* buffer, size_t capacity) {
    if (!type || !buffer || !capacity) {
        return 0u;
    }

    StringBuilder str(buffer, capacity);
    FormatTypeName(&str, type);
    return str.length();
}
