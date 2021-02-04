// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/memory.h>

#include <array>
#include <type_traits>

#include <gtest/gtest.h>

namespace {

constexpr bool IsAddressOfConstexpr() {
  int a = 1;
  return cpp17::addressof(a) == &a;
}

TEST(MemoryTest, AddressOfReturnsCorrectPointer) {
  static_assert(IsAddressOfConstexpr() == true,
                "cpp17::addressof should be evaluated in constexpr.");
}

TEST(MemoryTest, AddressOfReturnsAddressNotOverridenOperator) {
  struct Misleading {
    constexpr Misleading* operator&() const { return nullptr; }
  };

  alignas(Misleading) std::array<char, sizeof(Misleading)> buffer;
  const Misleading* ptr = new (static_cast<void*>(buffer.data())) Misleading();
  const Misleading& misleading = *ptr;

  ASSERT_EQ(&misleading, nullptr);
  ASSERT_EQ(cpp17::addressof(misleading), ptr);
}

#if __cpp_lib_addressof_constexpr >= 201603L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

template <typename T>
constexpr void check_alias() {
  // Need so the compiler picks the right overload.
  constexpr T* (*cpp17_addressof)(T&) = &cpp17::addressof<T>;
  constexpr T* (*std_addressof)(T&) = &std::addressof<T>;
  static_assert(cpp17_addressof == std_addressof);
}

struct UserType {
  int var;
};

TEST(MemoryTest, AddressOfIsAliasForStdWhenAvailable) {
  check_alias<int>();
  check_alias<UserType>();
}
#endif

}  // namespace
