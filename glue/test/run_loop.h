// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_GLUE_TEST_RUN_LOOP_H_
#define APPS_LEDGER_GLUE_TEST_RUN_LOOP_H_

#include "lib/ftl/functional/closure.h"
#include "lib/ftl/time/time_delta.h"

namespace glue {
namespace test {

// Quit the current run loop.
void QuitLoop();

// Run the current run loop.
void RunLoop();

}  // namespace test
}  // namespace glue

#endif  // APPS_LEDGER_GLUE_TEST_RUN_LOOP_H_
