// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspect.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <fs/dir_test_util.h>
#include <zxtest/zxtest.h>

class DriverHostInspectTestCase : public zxtest::Test {
 public:
  DriverHostInspectTestCase() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    loop_.StartThread("dh_inspect_test_thread");
  }

  DriverHostInspect& inspect() { return inspect_; }

 private:
  DriverHostInspect inspect_;
  async::Loop loop_;
};

TEST_F(DriverHostInspectTestCase, DirectoryEntries) {
  // Check that root inspect is created
  uint8_t buffer[4096];
  size_t length;
  {
    fs::vdircookie_t cookie = {};
    EXPECT_EQ(inspect().diagnostics_dir().Readdir(&cookie, buffer, sizeof(buffer), &length), ZX_OK);
    fs::DirentChecker dc(buffer, length);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEntry("root.inspect", V_TYPE_FILE);
    dc.ExpectEnd();
  }
}
