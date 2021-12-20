// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_PHDR_ERROR_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_PHDR_ERROR_H_

#include "../constants.h"
#include "const-string.h"

namespace elfldltl::internal {

// This is specialized below to provide constexpr-substitutable name strings.
// Not all tag values have specializations, only those that appear in
// phdr-related errors.
template <ElfPhdrType Type>
inline constexpr auto kPhdrTypeName = []() {
  static_assert(Type != Type, "missing specialization");
  return internal::ConstString("");
}();

// Specializations for all the phdr types used in error messages.
template <>
inline constexpr auto kPhdrTypeName<ElfPhdrType::kDynamic> = ConstString("PT_DYNAMIC");
template <>
inline constexpr auto kPhdrTypeName<ElfPhdrType::kInterp> = ConstString("PT_INTERP");
template <>
inline constexpr auto kPhdrTypeName<ElfPhdrType::kEhFrameHdr> = ConstString("PT_GNU_EH_FRAME");
template <>
inline constexpr auto kPhdrTypeName<ElfPhdrType::kRelro> = ConstString("PT_GNU_RELRO");

template <ElfPhdrType Type>
struct PhdrError {
  static constexpr auto kDuplicateHeader =  //
      ConstString("too many ") + kPhdrTypeName<Type> + " headers; expected at most one";
};

}  // namespace elfldltl::internal

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_PHDR_ERROR_H_
