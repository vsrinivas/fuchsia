// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __cplusplus
namespace zxtest {
namespace test {

// Because we are checking that the user exposed macros work correctly, we need a way for checking
// that all went well. Independently of the body of the tests. This allows registering arbitrary
// function pointers which verify that the test described in each file suceeded.
void AddCheckFunction(void (*check)(void));

// Call all registered functions. Uses ZX_ASSERT for verification, so on fail this will crash. Its
// better than relying on the system under test to verify that the same system is working.
void CheckAll();

} // namespace test
} // namespace zxtest

extern "C" {
#endif

void zxtest_add_check_function(void (*check)(void));

#ifdef __cplusplus
}
#endif
