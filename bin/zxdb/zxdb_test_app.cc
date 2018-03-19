// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <zircon/syscalls.h>

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
  // Print out the address of main to the system debug log.
  char buf[128];
  snprintf(buf, sizeof(buf), "zxdb_test_app, &main = 0x%llx\n",
           static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(&main)));
  zx_debug_write(buf, strlen(buf));

  DebugBreak();
  return 0;
}
