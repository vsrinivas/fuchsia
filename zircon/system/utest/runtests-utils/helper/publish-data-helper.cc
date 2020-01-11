// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmo.h>
#include <zircon/sanitizer.h>
#include <zxtest/zxtest.h>

namespace {

constexpr char kTestName[] = "test";
constexpr char kTestData[] = "test";
constexpr char kTestMessage[] = "{{{dumpfile:test:test}}}";

TEST(RunTestHelper, PublishData) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  vmo.set_property(ZX_PROP_NAME, kTestName, sizeof(kTestName) - 1);
  __sanitizer_publish_data(kTestData, vmo.release());
  __sanitizer_log_write(kTestMessage, sizeof(kTestMessage) - 1);
}

}  // anonymous namespace
