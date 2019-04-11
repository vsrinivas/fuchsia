// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect-vmo/inspect.h>
#include <lib/inspect-vmo/snapshot.h>
#include <lib/inspect-vmo/state.h>
#include <unittest/unittest.h>

using inspect::vmo::Inspector;
using inspect::vmo::Object;

namespace {

bool CreateDeleteActive() {
    BEGIN_TEST;

    Object object;

    {
        auto inspector = std::make_unique<Inspector>();
        object = inspector->CreateObject("object");
        EXPECT_TRUE(object);
        Object child = object.CreateChild("child");
        EXPECT_TRUE(child);
    }

    EXPECT_TRUE(object);

    Object child = object.CreateChild("child");
    EXPECT_TRUE(child);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(InspectTests)
RUN_TEST(CreateDeleteActive)
END_TEST_CASE(InspectTests)
