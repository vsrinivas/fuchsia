// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_std_string.h"

#include "src/developer/debug/zxdb/expr/format.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"

namespace zxdb {

// A hardcoded pretty-printer for our std::string implementation.
//
// Long-term, we'll want a better pretty-printing system that's more extensible and versionable
// with our C++ library. This is a first step to designing such a system.
//
// In libc++ std::string is an "extern template" which means that the char specialization of
// basic_string is in the shared library. Without symbols for libc++, there is no definition for
// std::string.
//
// As of this writing our libc++ doesn't have symbols, and it's also nice to allow people to
// print strings in their own program without all of the lib++ symbols (other containers don't
// require this so it can be surprising).
//
// As a result, this pretty-printer is designed to work with no symbol information, and getting
// a value with no size (the expression evaluator won't know what size to make in many cases).
// This complicates it considerably, but std::string is likely the only class that will need such
// handling.
//
// THE DEFINITION
// --------------
//
// Our lic++'s std::string implementation has two modes, a "short" mode where the string is stored
// inline in the string object, and a "long" mode where it stores a pointer to a heap-allocated
// buffer. These modes are differentiated with a bit on the last byte of the storage.
//
//   class basic_string {
//     // For little-endian:
//     static const size_type __short_mask = 0x80;
//     static const size_type __long_mask  = ~(size_type(~0) >> 1);  // High bit set.
//
//     bool is_long() const {return __r_.__s.__size_ & __short_mask; }
//
//     struct __rep {
//       // Long is used when "__s.__size_ & __short_mask" is true.
//       union {
//         struct __long {
//           value_type* __data_;
//           size_t __size_;
//           size_t __cap_;  // & with __long_mask to get.
//         } __l;
//
//         struct __short {
//           char value_type[23]
//           // padding of sizeof(char) - 1
//           struct {
//             unsigned char __size_;
//           };
//         } __s;
//
//         __raw __r;  // Can ignore, used only for rapidly copying the representation.
//       };
//     };
//
//     // actually "__compressed_pair<__rep, allocator> __r_" but effectively:
//     compressed_pair __r_;
//   };

namespace {

constexpr uint32_t kStdStringSize = 24;

void FormatStdStringMemory(const std::vector<uint8_t>& mem, FormatNode* node,
                           const FormatOptions& options, fxl::RefPtr<EvalContext> context,
                           fit::deferred_callback cb) {
  node->set_type("std::string");
  if (mem.size() != kStdStringSize) {
    node->set_err(Err("Invalid."));
    node->set_state(FormatNode::kDescribed);
    return;
  }

  // Offset from beginning of the object to "__short.__size_" (last byte).
  constexpr size_t kShortSizeOffset = 23;

  // Bit that indicates the "short" representation.
  constexpr uint64_t kShortMask = 0x80;

  // Offsets within the data for the "long" representation.
  constexpr uint64_t kLongPtrOffset = 0;
  constexpr uint64_t kLongSizeOffset = 8;

  auto char_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSignedChar, 1, "char");

  if (mem[kShortSizeOffset] & kShortMask) {
    // Long representation (with pointer).
    uint64_t data_ptr;
    memcpy(&data_ptr, &mem[kLongPtrOffset], sizeof(uint64_t));

    uint64_t string_size;
    memcpy(&string_size, &mem[kLongSizeOffset], sizeof(uint64_t));

    FormatCharPointerNode(node, data_ptr, char_type.get(), options, context, std::move(cb));
  } else {
    // Using the short inline data representation.
    size_t string_size = mem[kShortSizeOffset];
    FormatCharArrayNode(node, char_type, &mem[0], string_size, true, false);
  }
}

}  // namespace

void PrettyStdString::Format(FormatNode* node, const FormatOptions& options,
                             fxl::RefPtr<EvalContext> context, fit::deferred_callback cb) {
  const ExprValue& value = node->value();
  if (value.data().size() == kStdStringSize) {
    // Have all the data.
    FormatStdStringMemory(value.data(), node, options, context, std::move(cb));
  } else {
    // Normally when we have a std::string we won't have the data because the definition is
    // missing. But the "source" will usually be set and we can go fetch the right amount of data.
    if (value.source().type() == ExprValueSource::Type::kMemory && value.source().address() != 0) {
      context->GetDataProvider()->GetMemoryAsync(
          value.source().address(), kStdStringSize,
          [weak_node = node->GetWeakPtr(), options, context, cb = std::move(cb)](
              const Err& err, std::vector<uint8_t> data) mutable {
            if (!weak_node)
              return;
            if (err.has_error()) {
              weak_node->set_err(err);
              weak_node->set_state(FormatNode::kDescribed);
            } else {
              FormatStdStringMemory(data, weak_node.get(), options, context, std::move(cb));
            }
          });
    } else {
      node->set_err(Err("<Missing definition>"));
      node->set_state(FormatNode::kDescribed);
    }
  }
}

}  // namespace zxdb
