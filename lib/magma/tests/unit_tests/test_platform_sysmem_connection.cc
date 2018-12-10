// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_sysmem_connection.h"
#include "gtest/gtest.h"

class TestPlatformSysmemConnection {
public:
    static void TestCreate()
    {
        auto connection = magma::PlatformSysmemConnection::Create();

        EXPECT_NE(nullptr, connection.get());
    }
};

TEST(PlatformSysmemConnection, Create) { TestPlatformSysmemConnection::TestCreate(); }
