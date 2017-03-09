// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd.h"
#include "gtest/gtest.h"

TEST(MsdDriver, CreateAndDestroy)
{
    msd_driver_t* driver = msd_driver_create();
    ASSERT_NE(driver, nullptr);

    msd_driver_destroy(driver);
}
