// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_BOOT_OPTIONS_INCLUDE_LIB_BOOT_OPTIONS_TEST_TYPES_H_
#define ZIRCON_KERNEL_LIB_BOOT_OPTIONS_INCLUDE_LIB_BOOT_OPTIONS_TEST_TYPES_H_

// This declares special types used for BootOptions members used only in tests.

// This is used by the test_enum item in options.inc, for test cases.
enum class TestEnum {
  kDefault,
  kValue1,
  kValue2,
};

// This is used by the test_struct item in options.inc, for test cases.
struct TestStruct {
  constexpr bool operator==(const TestStruct& o) const { return o.present == present; }
  constexpr bool operator!=(const TestStruct& o) const { return !(*this == o); }

  bool present = false;
};

#endif  // ZIRCON_KERNEL_LIB_BOOT_OPTIONS_INCLUDE_LIB_BOOT_OPTIONS_TEST_TYPES_H_
