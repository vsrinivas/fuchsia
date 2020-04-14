// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspect.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

//#include <unittest/unittest.h>
#include <zxtest/zxtest.h>

namespace {

// Helper class for checking directory entiries
// TODO(puneetha): Move this to a test util header in libfs
class DirentChecker {
 public:
  DirentChecker(const void* buffer, size_t length)
      : current_(reinterpret_cast<const uint8_t*>(buffer)), remaining_(length) {}

  void ExpectEnd() { EXPECT_EQ(0u, remaining_); }

  void ExpectEntry(const char* name, uint32_t vtype) {
    ASSERT_NE(0u, remaining_);
    auto entry = reinterpret_cast<const vdirent_t*>(current_);
    size_t entry_size = entry->size + sizeof(vdirent_t);
    ASSERT_GE(remaining_, entry_size);
    current_ += entry_size;
    remaining_ -= entry_size;
    EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>(name),
                    reinterpret_cast<const uint8_t*>(entry->name), strlen(name), "name");
    EXPECT_EQ(VTYPE_TO_DTYPE(vtype), entry->type);
  }

 private:
  const uint8_t* current_;
  size_t remaining_;
};
}  // namespace

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

TEST_F(InspectManagerTestCase, Init) {
  // Check that sub-directories are created
  uint8_t buffer[4096];
  size_t length;
  {
    fs::vdircookie_t cookie = {};
    EXPECT_EQ(inspect_manager().diagnostics_dir().Readdir(&cookie, buffer, sizeof(buffer), &length),
              ZX_OK);
    DirentChecker dc(buffer, length);
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
    DirentChecker dc(buffer, length);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEntry("dm.inspect", V_TYPE_FILE);
    dc.ExpectEnd();
  }
}
