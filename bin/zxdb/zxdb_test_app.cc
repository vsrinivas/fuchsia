// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unistd.h>

// This is a simple app for testing various aspects of the debugger. To build,
// set include_test_app to true in the BUILD.gn file in this directory.

// The binary will end up in /system/test/zxdb_test_app.

void DebugBreak() {
#if defined(__x86_64__)
  __asm__ volatile("int3");
#elif defined(__aarch64__)
  __asm__ volatile("brk 0");
#else
  #error
#endif
}

int main(int arch, char* argv[]) {
  DebugBreak();
  return 0;
}
