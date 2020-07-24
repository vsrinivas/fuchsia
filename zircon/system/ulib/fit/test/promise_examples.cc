// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "examples/promise_example1.h"
#include "examples/promise_example2.h"

namespace {

TEST(PromiseExamples, example1) { promise_example1::run(); }

TEST(PromiseExamples, example2) { promise_example2::run(); }

}  // namespace
