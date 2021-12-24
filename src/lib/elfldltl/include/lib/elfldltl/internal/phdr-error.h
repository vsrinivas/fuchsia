// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_PHDR_ERROR_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_PHDR_ERROR_H_

#include "../constants.h"
#include "const-string.h"

namespace elfldltl::internal {

// This is specialized below to provide constexpr-substitutable name strings.
template <ElfPhdrType Type>
inline constexpr auto kPhdrTypeName = []() {
  static_assert(Type != Type, "missing specialization");
  return internal::ConstString("");
}();

// Specializations for all the phdr types.
template <>
inline constexpr auto kPhdrTypeName<ElfPhdrType::kNull> = ConstString("PT_NULL");
template <>
inline constexpr auto kPhdrTypeName<ElfPhdrType::kLoad> = ConstString("PT_LOAD");
template <>
inline constexpr auto kPhdrTypeName<ElfPhdrType::kDynamic> = ConstString("PT_DYNAMIC");
template <>
inline constexpr auto kPhdrTypeName<ElfPhdrType::kInterp> = ConstString("PT_INTERP");
template <>
inline constexpr auto kPhdrTypeName<ElfPhdrType::kNote> = ConstString("PT_NOTE");
template <>
inline constexpr auto kPhdrTypeName<ElfPhdrType::kTls> = ConstString("PT_TLS");
template <>
inline constexpr auto kPhdrTypeName<ElfPhdrType::kEhFrameHdr> = ConstString("PT_GNU_EH_FRAME");
template <>
inline constexpr auto kPhdrTypeName<ElfPhdrType::kStack> = ConstString("PT_GNU_STACK");
template <>
inline constexpr auto kPhdrTypeName<ElfPhdrType::kRelro> = ConstString("PT_GNU_RELRO");

template <ElfPhdrType Type>
struct PhdrError {
  static constexpr auto kDuplicateHeader =  //
      ConstString("too many ") + kPhdrTypeName<Type> + " headers; expected at most one";

  static constexpr auto kUnknownFlags =  //
      kPhdrTypeName<Type> + " header has unrecognized flags (other than PF_R, PF_W, PF_X)";

  static constexpr auto kBadAlignment =  //
      kPhdrTypeName<Type> + " header has `p_align` that is not zero or a power of two";

  static constexpr auto kUnalignedVaddr =  //
      kPhdrTypeName<Type> + " header has `p_vaddr % p_align != 0`";

  static constexpr auto kOffsetNotEquivVaddr =  //
      kPhdrTypeName<Type> + " header has incongruent `p_offset` and `p_vaddr` modulo `p_align`";

  static constexpr auto kFileszNotEqMemsz =  //
      kPhdrTypeName<Type> + " header has `p_filesz != p_memsz`";

  static constexpr auto kIncompatibleEntrySize =  //
      kPhdrTypeName<Type> + " segment size is not a multiple of entry size";

  static constexpr auto kIncompatibleEntryAlignment =  //
      kPhdrTypeName<Type> + " segment alignment is not a multiple of entry alignment";
};

}  // namespace elfldltl::internal

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_PHDR_ERROR_H_
