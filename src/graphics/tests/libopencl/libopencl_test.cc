// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include <CL/cl.h>
#include <gtest/gtest.h>

#include "src/lib/fxl/test/test_settings.h"

TEST(Libopencl, LoadIcd) {
  cl_int ret_val;
  cl_uint num_platforms;

  ret_val = clGetPlatformIDs(0, NULL, &num_platforms);

  ASSERT_EQ(ret_val, CL_SUCCESS);

  size_t param_val_ret_size;
  constexpr uint32_t kPlatformNameSize = 40;
  char platform_name[kPlatformNameSize];
  cl_uint i;

  cl_platform_id* all_platforms = (cl_platform_id*)malloc(num_platforms * sizeof(cl_platform_id));

  ret_val = clGetPlatformIDs(num_platforms, all_platforms, NULL);
  EXPECT_EQ(ret_val, CL_SUCCESS);

  for (i = 0; i < num_platforms; i++) {
    ret_val = clGetPlatformInfo(all_platforms[i], CL_PLATFORM_NAME, kPlatformNameSize,
                                (void*)platform_name, &param_val_ret_size);
    EXPECT_EQ(ret_val, CL_SUCCESS);
    EXPECT_EQ(strcmp(platform_name, "ICD_LOADER_TEST_OPENCL_STUB"), 0);
  }
}

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
