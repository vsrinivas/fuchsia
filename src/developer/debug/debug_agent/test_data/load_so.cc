// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>
#include <stdio.h>

// This simply loads and unloads a shared object for tests on the dynamic loader notifications.
int main(int arch, char** argv) {
  void* lib = dlopen("debug_agent_test_so.so", RTLD_GLOBAL);

  printf("Got library\n");

  dlclose(lib);
  return 0;
}
