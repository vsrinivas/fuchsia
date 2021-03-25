// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "examples/function_example1.h"
#include "examples/function_example2.h"

namespace {

TEST(FunctionExamples, example1) { function_example1::run(); }

TEST(FunctionExamples, example2) { function_example2::run(); }
}  // namespace
