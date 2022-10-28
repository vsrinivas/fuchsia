// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_UTILS_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_UTILS_H_

#include <fidl/fuchsia.sysmem/cpp/fidl.h>
#include <fidl/fuchsia.sysmem2/cpp/fidl.h>
#include <lib/sysmem-version/sysmem-version.h>

#include <type_traits>

#include <safemath/safe_conversions.h>

bool IsWriteUsage(const fuchsia_sysmem2::BufferUsage& buffer_usage);

bool IsCpuUsage(const fuchsia_sysmem2::BufferUsage& buffer_usage);

bool IsAnyUsage(const fuchsia_sysmem2::BufferUsage& buffer_usage);

namespace internal {

// Partial template specialization only works on classes / structs not functions, so impl is a
// struct.
//
// Base case lacks "do_cast" so intentionally won't compile.
template <typename TypeOut, typename TypeIn, typename Enable = void>
struct SafeCastImpl {};
template <typename TypeOut, typename TypeIn>
struct SafeCastImpl<
    TypeOut, TypeIn,
    std::enable_if_t<std::is_arithmetic_v<TypeOut> && std::is_arithmetic_v<TypeIn>>> {
  static constexpr TypeOut do_cast(TypeIn in) { return safemath::checked_cast<TypeOut>(in); }
};
template <typename TypeOut, typename TypeIn>
struct SafeCastImpl<
    TypeOut, TypeIn,
    std::enable_if_t<!std::is_arithmetic_v<TypeOut> || !std::is_arithmetic_v<TypeIn>>> {
  // We explicitly require any casting to/from fidl enum to be converting from/to the exact
  // underlying type, respectively.  Any additional conversion on top of that can be achieved with a
  // separate safe_cast<>().
  static_assert(!sysmem::IsFidlEnum_v<TypeOut> ||
                std::is_same_v<TypeIn, sysmem::FidlUnderlyingTypeOrType_t<TypeOut>>);
  static_assert(!sysmem::IsFidlEnum_v<TypeIn> ||
                std::is_same_v<TypeOut, sysmem::FidlUnderlyingTypeOrType_t<TypeIn>>);

  static constexpr TypeOut do_cast(TypeIn in) {
    if constexpr (sysmem::IsFidlEnum_v<TypeOut> || sysmem::IsFidlEnum_v<TypeIn>) {
      // LLCPP natural enums can be converted to/from their underlying type, but only explicitly.
      // Thanks to the static_assert()s above, we know that this static_cast<>() won't be narrowing
      // (or widening).
      return static_cast<TypeOut>(in);
    } else {
      // The -Wno-conversion cflags can only catch narrowing if the conversion is an implicit
      // conversion, so we need this to be an implicit conversion to cause any narrowing under this
      // template specialization to be a compile warning (treated as error).
      //
      // If hitting a compile warning about narrowing here, ensure that all narrowing safe_cast<>()s
      // are done with the source and destination types both is_arithmetic_v<> true.  This way, all
      // narrowing goes through safemath::checked_cast<>() above.  In other words, avoid doing both
      // a narrowing and a conversion to/from non-is_arithmetic_v<> in the same safe_cast<>().
      // Instead, do those two conversion steps separately, with separate safe_cast<>()s.
      return in;
    }
  }
};

}  // namespace internal

template <typename TypeOut, typename TypeIn>
constexpr TypeOut safe_cast(TypeIn in) {
  return internal::SafeCastImpl<TypeOut, TypeIn>::do_cast(in);
}

// debug_safe_cast<>() is _only_ for use in situations where we are very sure that
// safemath::checked_cast<>() would not ever complain.  The "safe"-ness is disabled in --release to
// avoid completely unnecessary checking in situations where the checking would be both very
// unnecessary and at least plausibly a performance concern.  Use safe_cast<>() instead in nearly
// all situations.  Buffer pattern filling is an example where debug_safe_cast<>() can be
// appropriate.
#if ZX_DEBUG_ASSERT_IMPLEMENTED
// This is "safe" in debug.  This way we can double-check when not --release that we're not messing
// up in a situation where we are "very sure" won't annoy safemath::checked_cast<>().
template <typename TypeOut, typename TypeIn>
constexpr TypeOut debug_safe_cast(TypeIn in) {
  return internal::SafeCastImpl<TypeOut, TypeIn>::do_cast(in);
}
#else
// Not "safe" in --release, but we only use debug_safe_cast<> when we're quite sure
// safemath::checked_cast<> can't possibly complain.
template <typename TypeOut, typename TypeIn>
constexpr TypeOut debug_safe_cast(TypeIn in) {
  return static_cast<TypeOut>(in);
}
#endif

// Ensure we get/permit a constexpr cast when possible.
static_assert(4u == safe_cast<uint32_t>(4ull));

// As of this comment, fidl natural types don't admit fidl::IsTable<>, but probably should.  Also
// fidl::ToWire() is the only way short of ::fidl::internal to find the wire type corresponding to
// a natural type, but can't be used in a SFINAE context because it triggers a static_assert().
//
// Once the issues in the previosu paragraph are addressed in FIDL generated code, we can simplify
// or eliminate the FIDL-related workaround templates below.

template <typename FidlType, typename enable = void>
class HasWireTypeTraits : public std::false_type {};
constexpr uint32_t kConstexprUint32 = 0;
template <typename FidlType>
class HasWireTypeTraits<
    FidlType,
    std::enable_if_t<std::is_same_v<decltype((kConstexprUint32)),
                                    decltype((fidl::TypeTraits<FidlType>::kPrimarySizeV1))>>>
    : public std::true_type {};

template <typename FidlType, typename enable = void>
class IsNaturalFidlTable : public std::false_type {};
template <typename FidlType>
class IsNaturalFidlTable<FidlType, std::enable_if_t<fidl::IsTable<FidlType>::value &&
                                                    !HasWireTypeTraits<FidlType>::value>>
    : public std::true_type {};

static_assert(IsNaturalFidlTable<fuchsia_sysmem2::BufferUsage>::value);
static_assert(!IsNaturalFidlTable<fuchsia_sysmem2::wire::BufferUsage>::value);
static_assert(!IsNaturalFidlTable<uint32_t>::value);
static_assert(!IsNaturalFidlTable<std::vector<fuchsia_sysmem2::BufferUsage>>::value);
static_assert(!IsNaturalFidlTable<std::vector<fuchsia_sysmem::BufferUsage>>::value);
static_assert(!IsNaturalFidlTable<fidl::VectorView<fuchsia_sysmem::BufferUsage>>::value);
static_assert(!IsNaturalFidlTable<fidl::VectorView<fuchsia_sysmem2::BufferUsage>>::value);
static_assert(!IsNaturalFidlTable<std::string>::value);
static_assert(!IsNaturalFidlTable<fidl::StringView>::value);

template <typename T>
void ignore_result(const T& to_ignore) {}

template <typename TypeOut, typename TypeIn>
TypeOut static_down_cast(TypeIn in) {
  TypeOut result = static_cast<TypeOut>(in);
  // This doesn't guarantee that |in| is actually an instance of sub-class TypeOut, but it does at
  // least check that TypeOut is a sub-class of TypeIn.
  static_assert(std::is_base_of_v<std::remove_reference_t<decltype(*in)>,
                                  std::remove_reference_t<decltype(*result)>>);
  return result;
}

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_UTILS_H_
