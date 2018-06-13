// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <glob.h>

#include "gtest/gtest.h"
#include "lib/fxl/strings/string_printf.h"

namespace component {
namespace {

TEST(ProbeHub, Component) {
  auto glob_str = fxl::StringPrintf("/hub/c/sysmgr/*/out/debug");
  glob_t globbuf;
  ASSERT_EQ(glob(glob_str.data(), 0, NULL, &globbuf), 0)
      << glob_str << " does not exist.";
  ASSERT_EQ(globbuf.gl_pathc, 1u);
  globfree(&globbuf);
}

TEST(ProbeHub, Realm) {
  auto glob_str = fxl::StringPrintf("/hub/r/sys/*/c/");
  glob_t globbuf;
  ASSERT_EQ(glob(glob_str.data(), 0, NULL, &globbuf), 0)
      << glob_str << " does not exist.";
  ASSERT_EQ(globbuf.gl_pathc, 1u);
  globfree(&globbuf);
}

TEST(ProbeHub, RealmSvc) {
  auto glob_str = fxl::StringPrintf("/hub/r/sys/*/svc/fuchsia.sys.Environment");
  glob_t globbuf;
  ASSERT_EQ(glob(glob_str.data(), 0, NULL, &globbuf), 0)
      << glob_str << " does not exist.";
  ASSERT_EQ(globbuf.gl_pathc, 1u);
  globfree(&globbuf);
}

}  // namespace
}  // namespace component
