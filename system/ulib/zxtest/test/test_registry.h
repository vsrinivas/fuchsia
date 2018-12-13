// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zxtest/base/test-driver.h>
#include <zxtest/base/test.h>

// Because this library defines a testing framework we cannot rely on it
// to correctly run our tests. Testing this library is done by manually
// adding functions into this header file and calling them in main.
//
// Assertions mechanisms are also unreliable, so use ZX_ASSERT instead.
// You should assume zxtest is not working when adding a test.
namespace zxtest {
namespace test {

// Verify that without errors, |Test::TestBody| is called after |Test::SetUp| and
// before |Test::TearDown|.
void TestRun();

// Verify that on |Test::Run| error |Test::TearDown| is still called.
void TestRunFailure();

// Verify that on |Test::SetUp| failure |Test::TearDown| is still called, but
// |Test::Run| is ignored.
void TestSetUpFailure();

} // namespace test
} // namespace zxtest
