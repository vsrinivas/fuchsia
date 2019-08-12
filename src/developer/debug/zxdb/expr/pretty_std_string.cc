// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_std_string.h"

#include "src/developer/debug/zxdb/expr/format.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"

namespace zxdb {

namespace {

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

constexpr uint32_t kStdStringSize = 24;

// Offset from beginning of the object to "__short.__size_" (last byte).
constexpr size_t kShortSizeOffset = 23;

// Bit that indicates the "short" representation.
constexpr uint64_t kShortMask = 0x80;

// Offsets within the data for the "long" representation.
constexpr uint64_t kLongPtrOffset = 0;
constexpr uint64_t kLongSizeOffset = 8;
constexpr uint64_t kLongCapacityOffset = 16;

fxl::RefPtr<BaseType> GetStdStringCharType() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSignedChar, 1, "char");
}
fxl::RefPtr<BaseType> GetSizeTType() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 8, "size_t");
}

// Returns true if this std::string uses the inline representation. It's assumed the data has
// al;ready been validated as being the correct length.
bool IsInlineString(const std::vector<uint8_t>& mem) {
  FXL_DCHECK(mem.size() == kStdStringSize);
  return !(mem[kShortSizeOffset] & kShortMask);
}

// Fills in the data pointer for the given std::string.
Err GetStringPtr(const ExprValue& value, uint64_t* ptr) {
  if (value.data().size() != kStdStringSize)
    return Err("Invalid std::string data.");

  if (IsInlineString(value.data())) {
    // The address is just the beginning of the string.
    if (value.source().type() != ExprValueSource::Type::kMemory || value.source().address() == 0)
      return Err("Can't get string pointer to a temporary.");
    *ptr = value.source().address();
  } else {
    memcpy(ptr, &value.data()[kLongPtrOffset], sizeof(uint64_t));
  }
  return Err();
}

Err GetStringSize(const std::vector<uint8_t>& mem, uint64_t* size) {
  if (mem.size() != kStdStringSize)
    return Err("Invalid std::string data.");

  if (IsInlineString(mem))
    *size = mem[kShortSizeOffset];
  else
    memcpy(size, &mem[kLongSizeOffset], sizeof(uint64_t));
  return Err();
}

Err GetStringCapacity(const std::vector<uint8_t>& mem, uint64_t* capacity) {
  if (mem.size() != kStdStringSize)
    return Err("Invalid std::string data.");

  if (IsInlineString(mem)) {
    *capacity = kShortSizeOffset - 1;  // Inline size is stuff before the short size minus null.
  } else {
    memcpy(capacity, &mem[kLongCapacityOffset], sizeof(uint64_t));

    // Mask off the high bit which is the "large" flag.
    *capacity &= 0x7fffffffffffffff;
  }
  return Err();
}

void FormatStdStringMemory(const std::vector<uint8_t>& mem, FormatNode* node,
                           const FormatOptions& options, fxl::RefPtr<EvalContext> context,
                           fit::deferred_callback cb) {
  node->set_type("std::string");
  if (mem.size() != kStdStringSize)
    return node->SetDescribedError(Err("Invalid."));

  auto char_type = GetStdStringCharType();

  uint64_t string_size = 0;
  if (Err err = GetStringSize(mem, &string_size); err.has_error())
    return node->SetDescribedError(err);

  if (IsInlineString(mem)) {
    FormatCharArrayNode(node, char_type, &mem[0], string_size, true, false);
  } else {
    // Long representation (with pointer).
    uint64_t data_ptr;
    memcpy(&data_ptr, &mem[kLongPtrOffset], sizeof(uint64_t));
    FormatCharPointerNode(node, data_ptr, char_type.get(), string_size, options, context,
                          std::move(cb));
  }
}

// Normally when we have a std::string we won't have the data because the definition is
// missing. But the "source" will usually be set and we can go fetch the right amount of data.
// This function calls the callback with a populated ExprValue if it can be made to have the correct
// size.
void EnsureStdStringMemory(fxl::RefPtr<EvalContext> context, const ExprValue& value,
                           fit::callback<void(const Err&, ExprValue)> cb) {
  if (value.data().size() != 0) {
    if (value.data().size() == kStdStringSize)
      return cb(Err(), value);
    return cb(Err("Invalid std::string type size."), ExprValue());
  }

  // Don't have the data, see if we can fetch it.
  if (value.source().type() != ExprValueSource::Type::kMemory || value.source().address() == 0)
    return cb(Err("Can't handle a temporary std::string."), ExprValue());

  context->GetDataProvider()->GetMemoryAsync(
      value.source().address(), kStdStringSize,
      [value, cb = std::move(cb)](const Err& err, std::vector<uint8_t> data) mutable {
        if (err.has_error())
          cb(err, ExprValue());
        else if (data.size() != kStdStringSize)
          cb(Err("Invalid memory."), ExprValue());
        else
          cb(Err(), ExprValue(value.type_ref(), std::move(data), value.source()));
      });
}

// Getters all need to do the same thing: ensure memory, error check, and then run on the result.
// This returns a callback that does that stuff, with the given "getter" implementation taking
// a complete string of a known correct size.
PrettyStdString::EvalFunction MakeGetter(
    fit::function<void(ExprValue, fit::callback<void(const Err&, ExprValue)>)> getter) {
  return
      [getter = std::move(getter)](fxl::RefPtr<EvalContext> context, const ExprValue& object_value,
                                   fit::callback<void(const Err&, ExprValue)> cb) mutable {
        EnsureStdStringMemory(context, object_value,
                              [context, cb = std::move(cb), getter = std::move(getter)](
                                  const Err& err, ExprValue value) mutable {
                                if (err.has_error())
                                  return cb(err, ExprValue());
                                getter(std::move(value), std::move(cb));
                              });
      };
}

}  // namespace

void PrettyStdString::Format(FormatNode* node, const FormatOptions& options,
                             fxl::RefPtr<EvalContext> context, fit::deferred_callback cb) {
  EnsureStdStringMemory(context, node->value(),
                        [weak_node = node->GetWeakPtr(), options, context, cb = std::move(cb)](
                            const Err& err, ExprValue value) mutable {
                          if (!weak_node)
                            return;
                          if (err.has_error()) {
                            weak_node->set_err(err);
                            weak_node->set_state(FormatNode::kDescribed);
                          } else {
                            FormatStdStringMemory(value.data(), weak_node.get(), options, context,
                                                  std::move(cb));
                          }
                        });
}

PrettyStdString::EvalFunction PrettyStdString::GetGetter(const std::string& getter_name) const {
  if (getter_name == "data" || getter_name == "c_str") {
    return MakeGetter([](ExprValue value, fit::callback<void(const Err&, ExprValue)> cb) {
      uint64_t ptr = 0;
      if (Err err = GetStringPtr(value, &ptr); err.has_error())
        return cb(err, ExprValue());

      auto char_ptr =
          fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, GetStdStringCharType());
      cb(Err(), ExprValue(ptr, char_ptr));
    });
  }
  if (getter_name == "size" || getter_name == "length") {
    return MakeGetter([](ExprValue value, fit::callback<void(const Err&, ExprValue)> cb) {
      uint64_t string_size = 0;
      if (Err err = GetStringSize(value.data(), &string_size); err.has_error())
        cb(err, ExprValue());
      cb(Err(), ExprValue(string_size, GetSizeTType()));
    });
  }
  if (getter_name == "capacity") {
    return MakeGetter([](ExprValue value, fit::callback<void(const Err&, ExprValue)> cb) {
      uint64_t cap = 0;
      if (Err err = GetStringCapacity(value.data(), &cap); err.has_error())
        return cb(err, ExprValue());
      cb(Err(), ExprValue(cap, GetSizeTType()));
    });
  }
  if (getter_name == "empty") {
    return MakeGetter([](ExprValue value, fit::callback<void(const Err&, ExprValue)> cb) {
      uint64_t string_size = 0;
      if (Err err = GetStringSize(value.data(), &string_size); err.has_error())
        return cb(err, ExprValue());
      cb(Err(), ExprValue(string_size == 0));
    });
  }

  return EvalFunction();
}

PrettyStdString::EvalArrayFunction PrettyStdString::GetArrayAccess() const {
  return [](fxl::RefPtr<EvalContext> context, const ExprValue& object_value, int64_t index,
            fit::callback<void(ErrOrValue)> cb) {
    EnsureStdStringMemory(
        context, object_value,
        [context, cb = std::move(cb), index](const Err& err, ExprValue value) mutable {
          if (err.has_error())
            return cb(err);
          if (IsInlineString(value.data())) {
            // Use the inline data. Need to range check since we're indexing into our local
            // address space.
            if (index >= static_cast<int64_t>(kShortSizeOffset) || index < 0)
              return cb(Err("String index out of range."));

            // Inline array starts from the beginning of the string.
            return cb(ExprValue(GetStdStringCharType(), {value.data()[index]},
                                value.source().GetOffsetInto(index)));
          } else {
            uint64_t ptr = 0;
            if (Err err = GetStringPtr(value, &ptr); err.has_error())
              return cb(err);

            context->GetDataProvider()->GetMemoryAsync(
                ptr, 1,
                [context, ptr, cb = std::move(cb)](const Err& err,
                                                   std::vector<uint8_t> data) mutable {
                  if (err.has_error())
                    return cb(err);
                  if (data.size() == 0)
                    return cb(Err("Invalid address 0x%" PRIx64, ptr));

                  cb(ExprValue(GetStdStringCharType(), {data[0]}, ExprValueSource(ptr)));
                });
          }
        });
  };
}

}  // namespace zxdb
