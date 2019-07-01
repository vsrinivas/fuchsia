// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TEST_DATA_TEST_SO_SYMBOLS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TEST_DATA_TEST_SO_SYMBOLS_H_

// Debug Agent Test Shared Object.
//
// This are the symbols that are meant to be exported both within a .so and a test binary.
// This will be used by tests to obtain the offset of these symbols within the dynamic library,
// which they load into their address space. By using this information, the agent can know where
// in the debug binary the actual symbol is, and this insert a breakpoint without needed DWARF
// symbols, which live in the host side.

// Mark the exported symbols to prevent the linker from stripping them.
#define EXPORT __attribute__((visibility("default")))
#define NOINLINE __attribute__((noinline))

extern "C" {

EXPORT extern bool gTestPassed;
EXPORT extern int gWatchpointVariable;

// Breakpoint Test -------------------------------------------------------------

EXPORT NOINLINE int InsertBreakpointFunction(int);
EXPORT NOINLINE int InsertBreakpointFunction2(int);
EXPORT NOINLINE void AnotherFunctionForKicks();

// Multithreaded Breakpoint Test -----------------------------------------------

EXPORT NOINLINE void MultithreadedFunctionToBreakOn();

// Watchpoint Test -------------------------------------------------------------

EXPORT NOINLINE void WatchpointFunction();

// Write Register Symbols ------------------------------------------------------

EXPORT NOINLINE void Test_BranchOnRAX();
EXPORT NOINLINE void Test_PCJump();
}

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TEST_DATA_TEST_SO_SYMBOLS_H_
