// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/test/test_with_coroutines.h"

namespace test {

TestWithCoroutines::TestWithCoroutines() {}

bool TestWithCoroutines::RunInCoroutine(
    std::function<void(coroutine::CoroutineHandler*)> run_test) {
  bool ended = false;
  coroutine_service_.StartCoroutine([&](coroutine::CoroutineHandler* handler) {
    run_test(handler);
    ended = true;
  });
  return RunLoopUntil([&] { return ended; });
}

}  // namespace test
