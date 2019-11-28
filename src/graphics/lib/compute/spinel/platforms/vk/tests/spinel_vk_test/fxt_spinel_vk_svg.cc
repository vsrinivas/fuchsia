// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fxt_spinel_vk_svg.h"

#include "spinel/ext/svg2spinel/svg2spinel.h"
#include "spinel/ext/transform_stack/transform_stack.h"
#include "svg/svg.h"

//
//
//

using namespace spinel::vk::test;

//
//
//

void
fxt_spinel_vk_svg::SetUp()
{
  //
  //
  //
  fxt_spinel_vk_render::SetUp();

  //
  // get the value param
  //
  param_spinel_vk_render const param = GetParam();

  //
  // load the svg doc
  //
  struct svg * svg = svg_parse(param.svg, false);

  ASSERT_NE(svg, nullptr);

  //
  // create a transform stack
  //
  struct transform_stack * const ts = transform_stack_create(16);

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
  // loop over the entire pipeline
  //
  for (uint32_t ii = 0; ii < param.loops; ii++)
    {
      // start over
      svg_rewind(svg);

      // define paths
      spn_path_t * const paths = spn_svg_paths_decode(svg, pb);

      // optional! this isn't required but can start work earlier
      spn(path_builder_flush(pb));

      // define rasters
      spn_raster_t * const rasters = spn_svg_rasters_decode(svg, rb, paths, ts);

      // optional! this isn't required but can start work earlier
      spn(raster_builder_flush(rb));

      // define styling and place rasters in composition
      spn_svg_layers_decode(svg, rasters, composition, styling, true);

      // explicitly seal the composition
      spn_composition_seal(composition);

      // explicitly seal the styling
      spn_styling_seal(styling);

      //
      // render
      //
      bool const is_first_loop = (ii == 0);
      bool const is_last_loop  = (ii + 1 == param.loops);

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

      // unseal and reset composition
      spn(composition_unseal(composition));
      spn(composition_reset(composition));

      // release paths
      spn_svg_paths_release(svg, context, paths);

      // release rasters
      spn_svg_rasters_release(svg, context, rasters);
    }

  //
  // wait for asynchronous path/raster releases to complete
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

  //
  // dispose of the svg doc
  //
  svg_dispose(svg);
}

//
//
//

void
fxt_spinel_vk_svg::TearDown()
{
  //
  // For now there is nothing new to tear down
  //
  fxt_spinel_vk_render::TearDown();
}

//
//
//
