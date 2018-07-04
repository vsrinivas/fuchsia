// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testutils/test_loop.h>
#include <lib/async/default.h>

namespace async {

TestLoop::TestLoop() {
    async_set_default_dispatcher(&dispatcher_);
}

TestLoop::~TestLoop() {
    async_set_default_dispatcher(nullptr);
}

} // namespace async
