// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_MEMORY_HELPERS_H_
#define TOOLS_FIDLCAT_LIB_MEMORY_HELPERS_H_

#if defined(__APPLE__)

#include <libkern/OSByteOrder.h>

#define le16toh(x) OSSwapLittleToHostInt16(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)

#else
#include <endian.h>
#endif

namespace fidlcat {

namespace internal {

// These are convenience functions for reading little endian (i.e., FIDL wire
// format encoded) bits.
template <typename T>
class LeToHost {
 public:
  static T le_to_host(const T* ts);
};

template <typename T>
T LeToHost<T>::le_to_host(const T* bytes) {
  if constexpr (std::is_same<T, uint8_t>::value) {
    return *bytes;
  } else if constexpr (std::is_same<T, uint16_t>::value) {
    return le16toh(*bytes);
  } else if constexpr (std::is_same<T, uint32_t>::value) {
    return le32toh(*bytes);
  } else if constexpr (std::is_same<T, uint64_t>::value) {
    return le64toh(*bytes);
  } else if constexpr (std::is_same<T, uintptr_t>::value &&
                       sizeof(T) == sizeof(uint64_t)) {
    // NB: On Darwin, uintptr_t and uint64_t are different things.
    return le64toh(*bytes);
  }
}

template <typename T>
struct GetUnsigned {
  using type = typename std::conditional<std::is_same<float, T>::value,
                                         uint32_t, uint64_t>::type;
};

template <typename T, typename P>
T MemoryFrom(P bytes) {
  static_assert(std::is_pointer<P>::value,
                "MemoryFrom can only be used on pointers");
  using U = typename std::conditional<std::is_integral<T>::value,
                                      std::make_unsigned<T>,
                                      GetUnsigned<T>>::type::type;
  union {
    U uval;
    T tval;
  } u;
  u.uval = LeToHost<U>::le_to_host(reinterpret_cast<const U*>(bytes));
  return u.tval;
}

}  // namespace internal

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_MEMORY_HELPERS_H_
