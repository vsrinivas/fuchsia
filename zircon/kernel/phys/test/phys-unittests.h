// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_TEST_PHYS_UNITTESTS_H_
#define ZIRCON_KERNEL_PHYS_TEST_PHYS_UNITTESTS_H_

// These are in this directory.
bool stack_tests();

// These are in kernel/tests.
bool popcount_tests();
bool printf_tests();
bool relocation_tests();
bool string_view_tests();
bool unittest_tests();
bool zbitl_tests();

#endif  // ZIRCON_KERNEL_PHYS_TEST_PHYS_UNITTESTS_H_
