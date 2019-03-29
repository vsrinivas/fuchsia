// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/zxdb/symbols/type.h"

namespace zxdb {

// Represents an array. An array is similar to a pointer but we specifically
// know its an array and know its length.
//
// DWARF says an array *may* have a length, but in practice Clang and GCC
// both define int[] as a pointer. Therefore, we require arrays to have
// known lengths.
class ArrayType final : public Type {
 public:
  const ArrayType* AsArrayType() const override;

  const Type* value_type() const { return value_type_.get(); }
  size_t num_elts() const { return num_elts_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(ArrayType);
  FRIEND_MAKE_REF_COUNTED(ArrayType);

  // The actual type (rather than a LazySymbol) is passed to this constructor
  // because all Types expect to have their size set as a member, and we can't
  // compute the size of an array without knowing the size of the contained
  // elements.
  ArrayType(fxl::RefPtr<Type> value_type, size_t num_elts);
  ~ArrayType() override;

  std::string ComputeFullName() const override;

  // Normally array names are the contained type with a "[...]" on the end,
  // but nested array dimensions work in the other direction, so it will look
  // like "array[outer][inner]". This function takes a previously computed
  // substring for what should be "[outer]" and creates the final type name.
  std::string ComputeFullNameOfNestedArray(const std::string& outer_dims) const;

  const fxl::RefPtr<Type> value_type_;
  const size_t num_elts_;
};

}  // namespace zxdb
