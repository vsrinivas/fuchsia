// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_FXT_SPINEL_VK_SVG_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_FXT_SPINEL_VK_SVG_H_

//
// This fixture supports writing explicit Spinel tests.
//
// The SVG fixture subclasses this fixture to enable writing simple
// rendering tests.
//

#include "fxt_spinel_vk_render.h"

//
//
//

namespace spinel::vk::test {

//
//
//

struct fxt_spinel_vk_svg : public fxt_spinel_vk_render
{
  //
  //
  //
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

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_FXT_SPINEL_VK_SVG_H_
