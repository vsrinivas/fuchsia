// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <perftest/perftest.h>

namespace {

// Measure the time taken by getpid().
//
// On Linux, this acts as a test to measure syscall overhead.  getpid() is
// a good example of a very simple syscall.
//
// Note that glibc's getpid() wrapper function used to cache the process ID
// in user space, but that caching was removed in 2017, and the wrapper
// currently always does a syscall invocation.  See:
// https://sourceware.org/glibc/wiki/Release/2.25#pid_cache_removal
bool Getpid() {
  pid_t pid = getpid();
  perftest::DoNotOptimize(pid);
  return true;
}

void RegisterTests() { perftest::RegisterSimpleTest<Getpid>("Getpid"); }
PERFTEST_CTOR(RegisterTests)

}  // namespace
