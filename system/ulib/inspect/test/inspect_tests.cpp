// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/inspect.h>
#include <lib/inspect/snapshot.h>
#include <lib/inspect/state.h>
#include <unittest/unittest.h>

using inspect::Inspector;

namespace {

bool CreateClone() {
    BEGIN_TEST;

    auto inspector = fbl::make_unique<Inspector>();
    EXPECT_TRUE(inspector->GetReadOnlyVmoClone());

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(InspectTests)
RUN_TEST(CreateClone)
END_TEST_CASE(InspectTests)
