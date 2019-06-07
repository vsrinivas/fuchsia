// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// The segfault child is not used by the test.
// It exists for debugging purposes.
constexpr char kTestSegfaultChildName[] = "segfault";

// Used for testing the s/w breakpoint insn.
constexpr char kTestSwbreakChildName[] = "swbreak";

constexpr char kTestInferiorChildName[] = "inferior";

// Test that the current suspension state is being preserved when a call is
// made while the thread is in an starting exception.
constexpr char kTestSuspendOnStart[] = "suspend-on-start";

// The value returned by |test_inferior()|.
constexpr int kInferiorReturnCode = 1234;

int test_segfault();
int test_sw_break();
int test_inferior();
int test_suspend_on_start();
