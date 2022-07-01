// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/realmfuzzer/engine/runner-test.h"
#include "src/sys/fuzzing/realmfuzzer/engine/runner.h"

namespace fuzzing {
namespace {

#define RUNNER_TYPE RealmFuzzerRunner
#define RUNNER_TEST RealmFuzzerRunnerTest
#include "src/sys/fuzzing/common/runner-fatal-unittest.inc"
#undef RUNNER_TYPE
#undef RUNNER_TEST

}  // namespace
}  // namespace fuzzing
