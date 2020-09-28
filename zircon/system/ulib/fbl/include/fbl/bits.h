// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_BITS_H_
#define FBL_BITS_H_

#include <zircon/assert.h>

#include <cstring>
#include <type_traits>

namespace fbl {

// Extracts the bit range [HightBit:LowBit] (inclusive) from a numerical input.
template <size_t HighBit, size_t LowBit, typename ReturnType, typename SourceType>
constexpr inline ReturnType ExtractBits(SourceType input) {
  // +1 for inclusivity of the upper bound.
  constexpr auto bit_count = HighBit + 1 - LowBit;

  static_assert(HighBit >= LowBit, "High bit must be greater or equal to low bit.");
  static_assert(HighBit < (sizeof(SourceType) * 8), "Source value ends before high bit");
  static_assert(bit_count <= (sizeof(ReturnType) * 8),
                "Return type is not large enough to hold requested bits.");
  auto pow2 = static_cast<SourceType>(1) << bit_count;
  return static_cast<ReturnType>((input >> LowBit) & (pow2 - 1));
}

template <size_t Bit, typename ReturnType, typename SourceType>
constexpr inline ReturnType ExtractBit(SourceType input) {
  return ExtractBits<Bit, Bit, ReturnType, SourceType>(input);
}

// The following contains safe wrappers for numeric bitfields. You can read or
// write to the members which in the general case is undefined behavior.
// However, the standard carves a special exception for types that are
// standard-layout as long they share a common initial sequence which is
// defined (C++11) as:
//  "Two standard-layout structs share a common initial sequence
//   if corresponding members have layout-compatible types and both
//   are bit-fields with the same width for a sequence of one or more
//   initial members"
//
// You can use this code without macros. To do so the code should look
// something like this:
//
//      union MyClass {
//          uint32_t full_value = initial_value;    // optional
//          fbl::BitFieldMember<uint32_t, 0, 3> member1;
//          fbl::BitFieldMember<uint32_t, 3, 2> member1;
//          ...
//          fbl::BitFieldMember<uint32_t, p,q> memberN;
//      };
//
//  All the members should be one 'T' type and the union should not
//  include any other object.
//
//  The Macros simply remove the risk of accidentally violating the rules
//  at the price of ugly looking code:
//
//      FBL_BITFIELD_DEF_START(MyClass, uint32_t)
//          FBL_BITFIELD_MEMBER(member1, 0, 3);
//          FBL_BITFIELD_MEMBER(member2, 4, 2);
//          ...
//          FBL_BITFIELD_MEMBER(memberN, p, q);
//      FBL_BITFIELD_DEF_END();
//
//   The usage is simple. It behaves as a set of unsigned integers with
//   reduced ranges that are packed efficiently:
//
//   MyClass options_(initial_opts);
//   ....
//   options_.member1 = 5u;
//   options_.member2 = 3u;
//   ....
//   if (options_.member1 < 4u) { ..}
//   ....
//   uint32_t copy = options_;
//

template <typename T, size_t Offset, size_t BitCount>
class BitFieldMember {
 public:
  static_assert(std::is_unsigned<T>::value, "bitfield type must be unsigned");
  static_assert(Offset + BitCount <= (sizeof(T) * 8u), "offset or count is too large");

  static constexpr T Maximum = (T(1) << BitCount) - 1;
  static constexpr T Mask = Maximum << Offset;

  constexpr T maximum() const { return Maximum; }

  constexpr operator T() const { return (value_ >> Offset) & Maximum; }

  constexpr BitFieldMember& operator=(T new_value) {
    ZX_DEBUG_ASSERT(new_value <= Maximum);
    // Subtle code ahead!
    // In typical usage, the storage for type |value_| will be a member of a
    // union and not necessarily the active union member. C++11 §9.5.1
    // [class.union] permits "inspection" of non-active members so long as
    // the union follows other rules which we already rely on to read the value
    // of the bitfield and compute a new value.
    T temp = static_cast<T>((value_ & ~Mask) | (new_value << Offset));
    // Now that we have a new value, we need to write it to the underlying
    // storage. Since |value_| may not be the active union member we can't
    // assign directly but we can std::memcpy() into the storage holding the
    // value.  See issue 38296 for an example of direct assignment producing
    // the wrong result.
    std::memcpy(&value_, &temp, sizeof(T));
    return *this;
  }

  constexpr BitFieldMember& operator=(const BitFieldMember& other) {
    T new_value = other;
    *this = new_value;
    return *this;
  }

 private:
  T value_;
};

#define FBL_BITFIELD_DEF_START(Typename, T)                                  \
  union Typename {                                                           \
    static_assert(std::is_standard_layout_v<T>,                              \
                  "Storage type in bitfield union must be standard layout"); \
    using ValueType = T;                                                     \
    ValueType value;                                                         \
    constexpr explicit Typename(T v = 0) : value(v) {}                       \
    constexpr Typename& operator=(T v) {                                     \
      value = v;                                                             \
      return *this;                                                          \
    }                                                                        \
    constexpr operator T&() { return value; }                                \
    constexpr operator T() const { return value; }

#define FBL_BITFIELD_MEMBER(MemberName, offset, bits)                                     \
  static_assert(std::is_standard_layout_v<fbl::BitFieldMember<ValueType, offset, bits>>); \
  fbl::BitFieldMember<ValueType, offset, bits> MemberName

#define FBL_BITFIELD_DEF_END() }

}  // namespace fbl

#endif  // FBL_BITS_H_
