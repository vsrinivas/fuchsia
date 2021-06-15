// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_ARRAY_TYPE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_ARRAY_TYPE_H_

#include <optional>

#include "src/developer/debug/zxdb/symbols/type.h"

namespace zxdb {

// Represents an array. An array is similar to a pointer but we specifically know that it is an
// array and often know its length. Not much can be done with arrays with unknown lengths.
//
// DWARF says an array *may* have a length. Clang and GCC define int[] as a pointer so we expect
// all "real" arrays to have a length.
//
// The case that may not have lengths are extern definitions that refer to arrays. For example:
//
//   extern const char kFoo[];
//
// will be marked as an "external" variable with an array type with no length. Resolving the
// extern to getting the real variable definition will give an array type with a real length.
class ArrayType final : public Type {
 public:
  const Type* value_type() const { return value_type_.get(); }
  std::optional<size_t> num_elts() const { return num_elts_; }

 protected:
  // Symbol protected override.
  const ArrayType* AsArrayType() const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(ArrayType);
  FRIEND_MAKE_REF_COUNTED(ArrayType);

  // The actual type (rather than a LazySymbol) is passed to this constructor because all Types
  // expect to have their size set as a member, and we can't compute the size of an array without
  // knowing the size of the contained elements.
  ArrayType(fxl::RefPtr<Type> value_type, std::optional<size_t> num_elts);
  ~ArrayType() override;

  std::string ComputeFullName() const override;

  // Normally array names are the contained type with a "[...]" on the end, but nested array
  // dimensions work in the other direction, so it will look like "array[outer][inner]". This
  // function takes a previously computed substring for what should be "[outer]" and creates the
  // final type name.
  std::string ComputeFullNameOfNestedArray(const std::string& outer_dims) const;

  const fxl::RefPtr<Type> value_type_;
  const std::optional<size_t> num_elts_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_ARRAY_TYPE_H_
