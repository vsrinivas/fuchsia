// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include "garnet/lib/magma/tests/helper/config_namespace_helper.h"

#if defined(MAGMA_USE_SHIM)
#include "vulkan_shim.h"
#endif

int main(int argc, char** argv)
{
#if defined(MAGMA_USE_SHIM)
    VulkanShimInit();
#endif

    if (!InstallConfigDirectoryIntoGlobalNamespace()) {
        return 1;
    }

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
