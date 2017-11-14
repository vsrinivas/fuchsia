// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_TESTS_UTIL_TEST_WAITER_H_
#define LIB_FIDL_CPP_BINDINGS_TESTS_UTIL_TEST_WAITER_H_

namespace fidl {
namespace test {
// Initialize the async waiter for this thread.
void InitAsyncWaiter();
// Does a non-blocking wait on all async-waited handles, and dispatches all
// the ones that are ready. Repeatedly does this until no handles are ready.
void WaitForAsyncWaiter();
// Cancels all async-waited handles.
void ClearAsyncWaiter();
}  // namespace test
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_TESTS_UTIL_TEST_WAITER_H_
