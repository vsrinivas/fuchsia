// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fxl/test/test_settings.h"
#include "gtest/gtest.h"

#if defined(MAGMA_USE_SHIM)
#include "vulkan_shim.h"
#endif

int main(int argc, char** argv)
{
    if (!fxl::SetTestSettings(argc, argv)) {
        return EXIT_FAILURE;
    }

#if defined(MAGMA_USE_SHIM)
    VulkanShimInit();
#endif
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
