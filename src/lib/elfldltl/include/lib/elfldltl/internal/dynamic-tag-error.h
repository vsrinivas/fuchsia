// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_DYNAMIC_TAG_ERROR_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_DYNAMIC_TAG_ERROR_H_

#include "../constants.h"
#include "const-string.h"

namespace elfldltl::internal {

template <ElfDynTag Tag>
struct DynamicTagType {};

// This is specialized below to provide constexpr-substitutable name strings.
// Not all tag values have specializations, only those used with SizedArray.
template <ElfDynTag Tag>
inline constexpr auto kDynamicTagName = []() {
  using TagType = DynamicTagType<Tag>;
  static_assert(!std::is_same_v<TagType, TagType>, "missing specialization");
  return internal::ConstString("");
}();

// Specializations for all the dynamic tag strings used in error messages.
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kNull> = ConstString("DT_NULL");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kRelr> = ConstString("DT_RELR");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kRelrSz> = ConstString("DT_RELRSZ");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kRel> = ConstString("DT_REL");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kRelSz> = ConstString("DT_RELSZ");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kRelCount> = ConstString("DT_RELCOUNT");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kRela> = ConstString("DT_RELA");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kRelaSz> = ConstString("DT_RELASZ");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kRelaCount> = ConstString("DT_RELACOUNT");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kJmpRel> = ConstString("DT_JMPREL");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kPltRelSz> = ConstString("DT_PLTRELSZ");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kPltRel> = ConstString("DT_PLTREL");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kStrTab> = ConstString("DT_STRTAB");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kStrSz> = ConstString("DT_STRSZ");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kInitArray> = ConstString("DT_INIT_ARRAY");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kInitArraySz> = ConstString("DT_INIT_ARRAYSZ");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kFiniArray> = ConstString("DT_FINI_ARRAY");
template <>
inline constexpr auto kDynamicTagName<ElfDynTag::kFiniArraySz> = ConstString("DT_FINI_ARRAYSZ");

template <ElfDynTag AddressTag, ElfDynTag SizeBytesTag, ElfDynTag CountTag>
struct DynamicTagError {
  static constexpr auto kMissingAddress =  //
      kDynamicTagName<SizeBytesTag> + " without " + kDynamicTagName<AddressTag>;

  static constexpr auto kMissingSize =  //
      kDynamicTagName<AddressTag> + " without " + kDynamicTagName<SizeBytesTag>;

  static constexpr auto kMisalignedAddress =  //
      kDynamicTagName<AddressTag> + " has misaligned address";

  static constexpr auto kMisalignedSize =  //
      kDynamicTagName<SizeBytesTag> + " not a multiple of " + kDynamicTagName<AddressTag> +
      " entry size";

  static constexpr auto kRead =  //
      ConstString("invalid address in ") + kDynamicTagName<AddressTag> + " or invalid size in " +
      kDynamicTagName<SizeBytesTag>;

  static constexpr auto kInvalidCount =
      kDynamicTagName<CountTag> + " too large for " + kDynamicTagName<SizeBytesTag>;
};

}  // namespace elfldltl::internal

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_DYNAMIC_TAG_ERROR_H_
