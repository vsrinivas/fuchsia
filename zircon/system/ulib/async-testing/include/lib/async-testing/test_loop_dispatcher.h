// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_TESTING_TEST_LOOP_DISPATCHER_H_
#define LIB_ASYNC_TESTING_TEST_LOOP_DISPATCHER_H_

#include <lib/async-testing/test_subloop.h>
#include <lib/async/dispatcher.h>

namespace async {

// Creates a new async dispatcher-based test loop. Returns the async dispatcher
// in |dispatcher| and the interface to control it in |loop|.
void NewTestLoopDispatcher(async_dispatcher_t** dispatcher, async_test_subloop_t** loop);

}  // namespace async

#endif  // LIB_ASYNC_TESTING_TEST_LOOP_DISPATCHER_H_
