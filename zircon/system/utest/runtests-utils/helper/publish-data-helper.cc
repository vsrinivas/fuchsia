// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmo.h>
#include <zircon/sanitizer.h>
#include <zxtest/zxtest.h>

namespace {

const char kTestName[] = "test";
const char kTestData[] = "test";

TEST(RunTestHelper, PublishData) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
    vmo.set_property(ZX_PROP_NAME, kTestName, sizeof(kTestName));
    __sanitizer_publish_data(kTestData, vmo.release());
}

} // anonymous namespace
