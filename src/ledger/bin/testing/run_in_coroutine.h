// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_RUN_IN_COROUTINE_H_
#define SRC_LEDGER_BIN_TESTING_RUN_IN_COROUTINE_H_

#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/ledger/bin/environment/environment.h"

namespace ledger {

// Runs the given test code in a coroutine, and only returns once the code completes.
//
// Returns true if the code finished executing, and false if the coroutine stopped without
// completing.
bool RunInCoroutine(async::TestLoop* test_loop, coroutine::CoroutineService* coroutine_service,
                    fit::function<void(coroutine::CoroutineHandler*)> run_test,
                    zx::duration delay = zx::sec(0));

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_RUN_IN_COROUTINE_H_
