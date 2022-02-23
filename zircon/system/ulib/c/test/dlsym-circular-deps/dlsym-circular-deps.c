// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>

#include <zxtest/zxtest.h>

TEST(DlsymCircularDepsTests, dlsym_circular_deps_test) {
  void* h = dlopen("shared_lib1.so", RTLD_LOCAL);
  ASSERT_NOT_NULL(h, "%s", dlerror());

  // shared_lib1 depends on shared_lib2 which depends on shared_lib3 which depends back on
  // shared_lib2. This symbol does not exist, but we just want to make sure we can exit
  // this function normally without hitting infinite recursion when cycling through DSO
  // dependencies.
  EXPECT_NULL(dlsym(h, "nonexistent_symbol"));

  EXPECT_EQ(dlclose(h), 0, "dlclose failed");
}
