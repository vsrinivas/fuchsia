// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/runner.h"

#include "src/sys/fuzzing/framework/engine/runner-test.h"

namespace fuzzing {
namespace {

#define RUNNER_TYPE RunnerImpl
#define RUNNER_TEST RunnerImplTest
#include "src/sys/fuzzing/common/runner-unittest.inc"
#undef RUNNER_TYPE
#undef RUNNER_TEST

}  // namespace
}  // namespace fuzzing
