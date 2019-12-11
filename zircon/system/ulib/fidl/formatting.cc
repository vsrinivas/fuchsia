// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

namespace {

class StringBuilder {
 public:
  StringBuilder(char* buffer, size_t capacity) : buffer_(buffer), capacity_(capacity) {}

  size_t length() const { return length_; }

  void Append(const char* data, size_t length) {
    size_t remaining = capacity_ - length_;
    if (length > remaining) {
      length = remaining;
    }
    memcpy(buffer_ + length_, data, length);
    length_ += length;
  }

  void Append(const char* data) { Append(data, strlen(data)); }

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

void FormatNullability(StringBuilder* str, FidlNullability nullable) {
  if (nullable == kFidlNullability_Nullable) {
    str->Append("?");
  }
}

void FormatEnumName(StringBuilder* str, const FidlCodedEnum* coded_enum) {
  if (coded_enum->name) {
    str->Append(coded_enum->name);
  } else {
    str->Append("enum");
  }
}

void FormatBitsName(StringBuilder* str, const FidlCodedBits* coded_bits) {
  if (coded_bits->name) {
    str->Append(coded_bits->name);
  } else {
    str->Append("bits");
  }
}

void FormatStructName(StringBuilder* str, const FidlCodedStruct* coded_struct) {
  if (coded_struct->name) {
    str->Append(coded_struct->name);
  } else {
    str->Append("struct");
  }
}

void FormatUnionName(StringBuilder* str, const FidlCodedUnion* coded_union) {
  if (coded_union->name) {
    str->Append(coded_union->name);
  } else {
    str->Append("union");
  }
}

void FormatTableName(StringBuilder* str, const FidlCodedTable* coded_table) {
  if (coded_table->name) {
    str->Append(coded_table->name);
  } else {
    str->Append("table");
  }
}

void FormatXUnionName(StringBuilder* str, const FidlCodedXUnion* coded_xunion) {
  if (coded_xunion->name) {
    str->Append(coded_xunion->name);
  } else {
    str->Append("xunion");
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
    case kFidlTypeEnum:
      FormatEnumName(str, &type->coded_enum);
      break;
    case kFidlTypeBits:
      FormatBitsName(str, &type->coded_bits);
      break;
    case kFidlTypeStruct:
      FormatStructName(str, &type->coded_struct);
      break;
    case kFidlTypeStructPointer:
      FormatStructName(str, type->coded_struct_pointer.struct_type);
      str->Append("?");
      break;
    case kFidlTypeUnion:
      FormatUnionName(str, &type->coded_union);
      break;
    case kFidlTypeUnionPointer:
      FormatUnionName(str, type->coded_union_pointer.union_type);
      str->Append("?");
      break;
    case kFidlTypeArray:
      str->Append("array<");
      FormatElementName(str, type->coded_array.element);
      str->Append(">");
      str->AppendPrintf(":%" PRIu32, type->coded_array.array_size / type->coded_array.element_size);
      break;
    case kFidlTypeString:
      str->Append("string");
      if (type->coded_string.max_size != FIDL_MAX_SIZE) {
        str->AppendPrintf(":%" PRIu32, type->coded_string.max_size);
      }
      FormatNullability(str, type->coded_string.nullable);
      break;
    case kFidlTypeHandle:
      str->Append("handle");
      if (type->coded_handle.handle_subtype) {
        str->Append("<");
        switch (type->coded_handle.handle_subtype) {
          case ZX_OBJ_TYPE_NONE:
            str->Append("handle");
            break;
          case ZX_OBJ_TYPE_BTI:
            str->Append("bti");
            break;
          case ZX_OBJ_TYPE_CHANNEL:
            str->Append("channel");
            break;
          case ZX_OBJ_TYPE_EVENT:
            str->Append("event");
            break;
          case ZX_OBJ_TYPE_EVENTPAIR:
            str->Append("eventpair");
            break;
          case ZX_OBJ_TYPE_EXCEPTION:
            str->Append("exception");
            break;
          case ZX_OBJ_TYPE_FIFO:
            str->Append("fifo");
            break;
          case ZX_OBJ_TYPE_GUEST:
            str->Append("guest");
            break;
          case ZX_OBJ_TYPE_INTERRUPT:
            str->Append("interrupt");
            break;
          case ZX_OBJ_TYPE_IOMMU:
            str->Append("iommu");
            break;
          case ZX_OBJ_TYPE_JOB:
            str->Append("job");
            break;
          case ZX_OBJ_TYPE_LOG:
            str->Append("log");
            break;
          case ZX_OBJ_TYPE_PAGER:
            str->Append("pager");
            break;
          case ZX_OBJ_TYPE_PCI_DEVICE:
            str->Append("pcidevice");
            break;
          case ZX_OBJ_TYPE_PMT:
            str->Append("pmt");
            break;
          case ZX_OBJ_TYPE_PORT:
            str->Append("port");
            break;
          case ZX_OBJ_TYPE_PROCESS:
            str->Append("process");
            break;
          case ZX_OBJ_TYPE_PROFILE:
            str->Append("profile");
            break;
          case ZX_OBJ_TYPE_RESOURCE:
            str->Append("resource");
            break;
          case ZX_OBJ_TYPE_SOCKET:
            str->Append("socket");
            break;
          case ZX_OBJ_TYPE_SUSPEND_TOKEN:
            str->Append("suspendtoken");
            break;
          case ZX_OBJ_TYPE_THREAD:
            str->Append("thread");
            break;
          case ZX_OBJ_TYPE_TIMER:
            str->Append("timer");
            break;
          case ZX_OBJ_TYPE_VCPU:
            str->Append("vcpu");
            break;
          case ZX_OBJ_TYPE_VMAR:
            str->Append("vmar");
            break;
          case ZX_OBJ_TYPE_VMO:
            str->Append("vmo");
            break;
          default:
            str->AppendPrintf("%" PRIu32, type->coded_handle.handle_subtype);
            break;
        }
        str->Append(">");
      }
      FormatNullability(str, type->coded_handle.nullable);
      break;
    case kFidlTypeVector:
      str->Append("vector<");
      FormatElementName(str, type->coded_vector.element);
      str->Append(">");
      if (type->coded_vector.max_count != FIDL_MAX_SIZE) {
        str->AppendPrintf(":%" PRIu32, type->coded_vector.max_count);
      }
      FormatNullability(str, type->coded_vector.nullable);
      break;
    case kFidlTypeTable:
      FormatTableName(str, &type->coded_table);
      break;
    case kFidlTypeXUnion:
      FormatXUnionName(str, &type->coded_xunion);
      break;
    case kFidlTypePrimitive:
      ZX_PANIC("unrecognized tag");
      break;
  }
}

}  // namespace

size_t fidl_format_type_name(const fidl_type_t* type, char* buffer, size_t capacity) {
  if (!type || !buffer || !capacity) {
    return 0u;
  }

  StringBuilder str(buffer, capacity);
  FormatTypeName(&str, type);
  return str.length();
}
