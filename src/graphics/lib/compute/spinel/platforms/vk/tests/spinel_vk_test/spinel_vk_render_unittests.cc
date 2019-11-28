// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fxt_spinel_vk_render.h"
#include "spinel/ext/color/color.h"
#include "spinel/ext/transform_stack/transform_stack.h"

//
//
//

namespace spinel::vk::test {

// alias for test output aesthetics
using spinel_vk_render = fxt_spinel_vk_render;
using param            = param_spinel_vk_render;

//
// explicitly render a 2x2 pixel black square
//
TEST_P(spinel_vk_render, example)
{
  // the param for this test
  param_spinel_vk_render const param = GetParam();

  //
  // create a transform stack
  //
  struct transform_stack * const ts = transform_stack_create(16);

  ASSERT_NE(ts, nullptr);

  transform_stack_push_scale(ts, 32.0f, 32.0f);

  //
  // create builders
  //
  spn_path_builder_t pb;

  spn(path_builder_create(context, &pb));

  spn_raster_builder_t rb;

  spn(raster_builder_create(context, &rb));

  //
  // create composition
  //
  spn_composition_t composition;

  spn(composition_create(context, &composition));

  spn(composition_set_clip(composition, param.clip.composition));

  //
  // create styling
  //
  spn_styling_t styling;

  // 4K layers, 16K cmds
  spn(styling_create(context, &styling, 4096, 16384));

  //
  // set up rendering extensions
  //
  VkBufferImageCopy const bic = {

    .bufferOffset      = 0,
    .bufferRowLength   = param.surface.width,
    .bufferImageHeight = param.surface.height,

    .imageSubresource = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                          .mipLevel       = 0,
                          .baseArrayLayer = 0,
                          .layerCount     = 1 },

    .imageOffset = { .x = 0, .y = 0, .z = 0 },
    .imageExtent = { .width  = param.surface.width,  //
                     .height = param.surface.height,
                     .depth  = 1 }
  };

  spn_vk_render_submit_ext_image_post_copy_to_buffer_t rs_image_post_copy_to_buffer = {

    .ext          = NULL,
    .type         = SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_POST_COPY_TO_BUFFER,
    .dst          = surface.h.dbi.buffer,
    .region_count = 1,
    .regions      = &bic
  };

  VkClearColorValue const ccv = { .float32 = { 1.0f, 1.0f, 1.0f, 1.0f } };

  spn_vk_render_submit_ext_image_pre_clear_t rs_pre_image_clear = {

    .ext   = NULL,
    .type  = SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_PRE_CLEAR,
    .color = &ccv,
  };

  spn_vk_render_submit_ext_image_pre_barrier_t rs_image_pre_barrier = {

    .ext        = NULL,
    .type       = SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_PRE_BARRIER,
    .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
    .src_qfi    = VK_QUEUE_FAMILY_IGNORED,
  };

  spn_vk_render_submit_ext_image_render_t rs_image_render = {

    .ext            = NULL,
    .type           = SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_RENDER,
    .image          = surface.d.image,
    .image_info     = surface.d.image_info,
    .submitter_pfn  = fxt_spinel_vk_render::spn_render_submitter,
    .submitter_data = NULL
  };

  spn_render_submit_t const rs = {

    .ext         = &rs_image_render,
    .styling     = styling,
    .composition = composition,
    .clip        = { param.clip.render[0],  // clang-format off
                     param.clip.render[1],
                     param.clip.render[2],
                     param.clip.render[3] }  // clang-format on
  };

  //
  // define path
  //
  spn(path_builder_begin(pb));

  spn(path_builder_move_to(pb, 2.0f, 2.0f));
  spn(path_builder_line_to(pb, 4.0f, 2.0f));
  spn(path_builder_line_to(pb, 4.0f, 4.0f));
  spn(path_builder_line_to(pb, 2.0f, 4.0f));
  spn(path_builder_line_to(pb, 2.0f, 2.0f));

  spn_path_t path;

  spn(path_builder_end(pb, &path));

  // this isn't necessary but can start work earlier
  spn(path_builder_flush(pb));

  //
  // define raster
  //
  spn(raster_builder_begin(rb));

  struct spn_clip const raster_clips[] = { { 0.0f, 0.0f, 0.0f, 0.0f } };

  spn(raster_builder_add(rb,  //
                         &path,
                         transform_stack_top_weakref(ts),
                         (spn_transform_t *)transform_stack_top_transform(ts),
                         NULL,
                         raster_clips,
                         1));

  spn_raster_t raster;

  spn(raster_builder_end(rb, &raster));

  // this isn't necessary but can start work earlier
  spn(raster_builder_flush(rb));

  //
  // place rasters into composition
  //
  uint32_t     layer_count = 1;
  spn_layer_id layer_id    = layer_count - 1;

  spn(composition_place(composition, &raster, &layer_id, NULL, 1));

  // seal the composition
  spn_composition_seal(composition);

  //
  // define the styling
  //
  spn_group_id group_id;

  spn(styling_group_alloc(styling, &group_id));

  {
    spn_styling_cmd_t * cmds_enter;

    spn(styling_group_enter(styling, group_id, 1, &cmds_enter));

    cmds_enter[0] = SPN_STYLING_OPCODE_COLOR_ACC_ZERO;
  }

  {
    spn_styling_cmd_t * cmds_leave;

    spn(styling_group_leave(styling, group_id, 4, &cmds_leave));

    float const background[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    // cmds[0-2]
    spn_styling_background_over_encoder(cmds_leave, background);

    cmds_leave[3] = SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE;
  }

  // this is the root group
  spn(styling_group_parents(styling, group_id, 0, NULL));

  // the range of the root group is maximal [0,layer_count)
  spn(styling_group_range_lo(styling, group_id, 0));
  spn(styling_group_range_hi(styling, group_id, layer_count - 1));

  // define layer styling
  {
    float rgba[4];

    color_rgb32_to_rgba_f32(rgba, 0x000000, 1.0f);

    color_srgb_to_linear_rgb_f32(rgba);

    color_premultiply_rgba_f32(rgba);

    spn_styling_cmd_t * cmds;

    spn(styling_group_layer(styling, group_id, layer_id, 5, &cmds));

    cmds[0] = SPN_STYLING_OPCODE_COVER_NONZERO;

    // encode solid fill and fp16v4 at cmds[1-3]
    spn_styling_layer_fill_rgba_encoder(cmds + 1, rgba);

    cmds[4] = SPN_STYLING_OPCODE_BLEND_OVER;
  }

  // seal the styling
  spn_styling_seal(styling);

  //
  // render
  //
  uint32_t const loop_count = 1;
  uint32_t       loop_idx   = 0;

  bool const is_first_loop = (loop_idx == 0);
  bool const is_last_loop  = (loop_idx + 1 == loop_count);

  // reset
  rs_image_render.ext = NULL;

  if (is_first_loop && is_last_loop)
    {
      rs_image_pre_barrier.ext         = rs_image_render.ext;      // pre-render transition
      rs_pre_image_clear.ext           = &rs_image_pre_barrier;    // pre-render clear
      rs_image_post_copy_to_buffer.ext = &rs_pre_image_clear.ext;  // post-render copy
      rs_image_render.ext              = &rs_image_post_copy_to_buffer;
    }
  else if (is_first_loop)
    {
      rs_image_pre_barrier.ext = rs_image_render.ext;    // pre-render transition
      rs_pre_image_clear.ext   = &rs_image_pre_barrier;  // pre-render clear
      rs_image_render.ext      = &rs_pre_image_clear;    // render
    }
  else if (is_last_loop)
    {
      rs_image_post_copy_to_buffer.ext = rs_image_render.ext;  // post-render-copy
      rs_image_render.ext              = &rs_image_post_copy_to_buffer;
    }

  spn(render(context, &rs));

  //
  // unseal and reset the composition
  //
  // note that this will block until the render is complete
  //
  spn(composition_unseal(composition));
  spn(composition_reset(composition));

  //////////////////////////
  //
  // unseal and reset the styling
  //
  spn(styling_unseal(styling));
  spn(styling_reset(styling));

  //////////////////////////
  //
  // release path
  //
  spn(path_release(context, &path, 1));

  //////////////////////////
  //
  // release raster
  //
  spn(raster_release(context, &raster, 1));

  //
  // wait for asynchronous releases
  //
  spn(vk_context_wait(context, 0, NULL, true, UINT64_MAX));

  //
  // checksum?
  //
  checksum();

  //
  // release the builders, composition and styling
  //
  spn(path_builder_release(pb));
  spn(raster_builder_release(rb));
  spn(composition_release(composition));
  spn(styling_release(styling));

  //
  // release the transform stack
  //
  transform_stack_release(ts);
}

//
// value parameterization is leveraged by the SVG fixture
//
param const params[] = {
  {
    .name      = "black_square_2x2",
    .surface   = { 1024, 1024 },  //
    .checksums = {                //
      { 0xFBF00004, {} }
    }
  },
};

//
//
//
INSTANTIATE_TEST_SUITE_P(spinel_vk_render_examples,  //
                         spinel_vk_render,           //
                         ::testing::ValuesIn(params),
                         fxt_spinel_vk_render::param_name);

}  // namespace spinel::vk::test

//
//
//
