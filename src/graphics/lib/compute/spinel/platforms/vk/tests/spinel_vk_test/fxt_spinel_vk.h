// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_FXT_SPINEL_VK_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_FXT_SPINEL_VK_H_

//
//
//

#include "env.h"
#include "gtest/gtest.h"
#include "spinel/spinel_vk.h"

//
//
//

namespace spinel::vk::test {

//
//
//

struct fxt_spinel_vk : public ::testing::Test
{
  //
  // environments are shared across tests in the suite
  //
  static env * shared_env;

  static void
  SetUpTestSuite();

  static void
  TearDownTestSuite();

  //
  //
  //
  spn_context_t context;

  void
  SetUp() override;

  void
  TearDown() override;
};

//
//
//

}  // namespace spinel::vk::test

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_FXT_SPINEL_VK_H_
