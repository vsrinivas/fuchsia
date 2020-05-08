// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspect.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <fs/dir_test_util.h>
#include <zxtest/zxtest.h>

class InspectManagerTestCase : public zxtest::Test {
 public:
  InspectManagerTestCase() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    loop_.StartThread("inspect_test_thread");
    inspect_manager_ = std::make_unique<InspectManager>(loop_.dispatcher());
  }

  InspectManager& inspect_manager() { return *inspect_manager_; }

 private:
  std::unique_ptr<InspectManager> inspect_manager_;
  async::Loop loop_;
};

TEST_F(InspectManagerTestCase, DirectoryEntries) {
  // Check that sub-directories are created
  uint8_t buffer[4096];
  size_t length;
  {
    fs::vdircookie_t cookie = {};
    EXPECT_EQ(inspect_manager().diagnostics_dir().Readdir(&cookie, buffer, sizeof(buffer), &length),
              ZX_OK);
    fs::DirentChecker dc(buffer, length);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEntry("driver_manager", V_TYPE_DIR);
    dc.ExpectEnd();
  }

  // Check entries of diagnostics/driver_manager
  {
    fbl::RefPtr<fs::Vnode> node;
    inspect_manager().diagnostics_dir().Lookup(&node, "driver_manager");
    fs::vdircookie_t cookie = {};
    EXPECT_EQ(node->Readdir(&cookie, buffer, sizeof(buffer), &length), ZX_OK);
    fs::DirentChecker dc(buffer, length);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEntry("driver_host", V_TYPE_DIR);
    dc.ExpectEntry("dm.inspect", V_TYPE_FILE);
    dc.ExpectEnd();
  }
}
