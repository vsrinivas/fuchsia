// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_FXT_SPINEL_VK_RENDER_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_FXT_SPINEL_VK_RENDER_H_

//
// This fixture supports writing explicit Spinel tests.
//
// The SVG fixture subclasses this fixture to enable writing simple
// rendering tests.
//

#include "fxt_spinel_vk.h"

//
//
//

namespace spinel::vk::test {

//
// We don't need to get too fancy here.  We're not implementing a true
// Value interface, rather we're just lumping in all the fields we might
// need in an explicit render.
//

struct param_spinel_vk_render
{
  char const * name;

  struct
  {
    uint32_t width;
    uint32_t height;
  } surface;

  char const * svg   = nullptr;
  uint32_t     loops = 1;

  uint32_t checksum;
};

//
//
//

struct fxt_spinel_vk_render : public fxt_spinel_vk,
                              public testing::WithParamInterface<param_spinel_vk_render>
{
  //
  //
  //
  struct surface
  {
    struct
    {
      VkImage               image;
      VkDeviceMemory        dm;
      VkDescriptorImageInfo image_info;
    } d;
    struct
    {
      VkDescriptorBufferInfo dbi;
      VkDeviceMemory         dm;
      void *                 map;
    } h;
  } surface;

  //
  //
  //
  void
  SetUp() override;

  void
  TearDown() override;

  //
  // test the surface
  //
  void
  checksum();

  //
  // param name suffix generator
  //
  static std::string
  param_name(testing::TestParamInfo<param_spinel_vk_render> const & info);

  //
  // simplest submitter
  //
  static void
  spn_render_submitter(VkQueue               queue,  //
                       VkFence               fence,  //
                       VkCommandBuffer const cb,     //
                       void *                data);
};

//
// FIXME(allanmac): Styling opcodes will be buried later
//

//
// clang-format off
//

#define SPN_STYLING_OPCODE_NOOP                        0

#define SPN_STYLING_OPCODE_COVER_NONZERO               1
#define SPN_STYLING_OPCODE_COVER_EVENODD               2
#define SPN_STYLING_OPCODE_COVER_ACCUMULATE            3
#define SPN_STYLING_OPCODE_COVER_MASK                  4

#define SPN_STYLING_OPCODE_COVER_WIP_ZERO              5
#define SPN_STYLING_OPCODE_COVER_ACC_ZERO              6
#define SPN_STYLING_OPCODE_COVER_MASK_ZERO             7
#define SPN_STYLING_OPCODE_COVER_MASK_ONE              8
#define SPN_STYLING_OPCODE_COVER_MASK_INVERT           9

#define SPN_STYLING_OPCODE_COLOR_FILL_SOLID            10
#define SPN_STYLING_OPCODE_COLOR_FILL_GRADIENT_LINEAR  11

#define SPN_STYLING_OPCODE_COLOR_WIP_ZERO              12
#define SPN_STYLING_OPCODE_COLOR_ACC_ZERO              13

#define SPN_STYLING_OPCODE_BLEND_OVER                  14
#define SPN_STYLING_OPCODE_BLEND_PLUS                  15
#define SPN_STYLING_OPCODE_BLEND_MULTIPLY              16
#define SPN_STYLING_OPCODE_BLEND_KNOCKOUT              17

#define SPN_STYLING_OPCODE_COVER_WIP_MOVE_TO_MASK      18
#define SPN_STYLING_OPCODE_COVER_ACC_MOVE_TO_MASK      19

#define SPN_STYLING_OPCODE_COLOR_ACC_OVER_BACKGROUND   20
#define SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE  21
#define SPN_STYLING_OPCODE_COLOR_ACC_TEST_OPACITY      22

#define SPN_STYLING_OPCODE_COLOR_ILL_ZERO              23
#define SPN_STYLING_OPCODE_COLOR_ILL_COPY_ACC          24
#define SPN_STYLING_OPCODE_COLOR_ACC_MULTIPLY_ILL      25

#define SPN_STYLING_OPCODE_COUNT                       26

//
//
//

}  // namespace spinel::vk::test

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_FXT_SPINEL_VK_RENDER_H_
