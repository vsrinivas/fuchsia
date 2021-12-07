// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_LSAN_DISABLER_H_
#define SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_LSAN_DISABLER_H_

#include <thread>

#include <sanitizer/lsan_interface.h>
#include <zxtest/zxtest.h>

namespace fidl_testing {

// Run a test with LSAN disabled.
template <typename Callable>
void RunWithLsanDisabled(Callable&& callable) {
#if __has_feature(address_sanitizer) || __has_feature(leak_sanitizer)
  // Disable LSAN for this thread while in scope. It is expected to leak by way
  // of a crash.
  __lsan::ScopedDisabler _;
#endif
  callable();
}

}  // namespace fidl_testing

#endif  // SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_LSAN_DISABLER_H_
