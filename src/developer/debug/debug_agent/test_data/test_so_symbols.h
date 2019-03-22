// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Debug Agent Test Shared Object.
//
// This are the symbols that are meant to be exported. asdgadga

// Mark the exported symbols to prevent the linker from stripping them.
#define EXPORT __attribute__((visibility("default")))
#define NOINLINE __attribute__((noinline))

extern "C" {

EXPORT extern bool gTestPassed;

// Breakpoint Test -------------------------------------------------------------

EXPORT NOINLINE int InsertBreakpointFunction(int);
EXPORT NOINLINE void AnotherFunctionForKicks();

// Write Register Symbols ------------------------------------------------------

EXPORT NOINLINE void Test_BranchOnRAX();
EXPORT NOINLINE void Test_PCJump();
}
