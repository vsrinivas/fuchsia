// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>

#include <zxtest/zxtest.h>

TEST(DlopenIndirectDepsTests, dlopen_indirect_deps_test) {
  void* h = dlopen("libdlopen-indirect-deps-test-module.so", RTLD_LOCAL);
  ASSERT_NOT_NULL(h, "%s", dlerror());

  EXPECT_NOT_NULL(dlsym(h, "module_symbol"), "symbol not found in dlopen'd lib");

  EXPECT_NOT_NULL(dlsym(h, "liba_symbol"), "symbol not found in dlopen'd lib's direct dependency");

  EXPECT_NOT_NULL(dlsym(h, "libb_symbol"),
                  "symbol not found in dlopen'd lib's indirect dependency");

  EXPECT_EQ(dlclose(h), 0, "dlclose failed");
}
