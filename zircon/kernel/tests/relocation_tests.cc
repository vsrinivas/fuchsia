// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/unittest/unittest.h>
#include <stdio.h>
#include <zircon/assert.h>

namespace {

// Hide a pointer from the compiler, preventing it from optimising
// away memory accesses we want it to perform.
template <typename T>
inline T* HidePointer(T* ptr) {
  // The following empty assembly block claims to both read and write
  // `ptr`, so that the compiler can no longer reason about the output
  // of this function.
  __asm__("" : /* outputs */ "+g"(ptr));
  return ptr;
}

bool test_static_pointer() {
  BEGIN_TEST;

  // Create a static pointer to another global.
  constexpr uint64_t kExpectedValue = 0x11223344'aabbccdd;
  static uint64_t static_value = kExpectedValue;
  static uint64_t* static_value_ptr = &static_value;  // expected to be patched

  EXPECT_EQ(**HidePointer(&static_value_ptr), kExpectedValue);

  END_TEST;
}

bool test_static_function_pointer() {
  BEGIN_TEST;

  // Set up a static variable containing a statically initialized function pointer.
  //
  // This requires a relocation to be processed to ensure that it points to the
  // right location.
  constexpr uint64_t kExpectedValue = 0xaabbccdd'12345678;
  constexpr auto callback = []() { return kExpectedValue; };
  static uint64_t (*callback_ptr)() = callback;  // expected to be patched

  // Call the function pointer.
  EXPECT_EQ((*HidePointer(&callback_ptr))(), kExpectedValue);

  END_TEST;
}

}  // namespace

// Set up a hierarchy class that requires use of dynamic dispatch, which may
// required vtables to have relocations applied to them.
//
// We put this in the global namespace to reduce the chance that the C++
// compiler will be able to optimise away the virtual dispatch.
class BaseClass {
 public:
  ~BaseClass() = default;
  virtual uint64_t Value() = 0;
};
class DerivedA : public BaseClass {
 public:
  static constexpr uint64_t kExpected = 0xaaaa'aaaa'aaaa'aaaa;
  uint64_t Value() override { return kExpected; }
};
class DerivedB : public BaseClass {
 public:
  static constexpr uint64_t kExpected = 0xbbbb'bbbb'bbbb'bbbb;
  uint64_t Value() override { return kExpected; }
};

namespace {

bool test_virtual_dispatch() {
  BEGIN_TEST;

  // Set up statically defined subclasses.
  static DerivedA derived_a;
  static DerivedB derived_b;
  static BaseClass* abstract_a = &derived_a;  // expected to be patched
  static BaseClass* abstract_b = &derived_b;  // expected to be patched

  // Call into the derived classes.
  EXPECT_EQ((*HidePointer(&abstract_a))->Value(), DerivedA::kExpected);
  EXPECT_EQ((*HidePointer(&abstract_b))->Value(), DerivedB::kExpected);

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(relocation_tests)
UNITTEST("static pointer", test_static_pointer)
UNITTEST("static function pointer", test_static_function_pointer)
UNITTEST("virtual dispatch", test_virtual_dispatch)
UNITTEST_END_TESTCASE(relocation_tests, "relocation", "relocation tests")
