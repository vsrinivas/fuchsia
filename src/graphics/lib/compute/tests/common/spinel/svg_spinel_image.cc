// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svg_spinel_image.h"

#include "spinel/ext/color/color.h"
#include "spinel/ext/transform_stack/transform_stack.h"
#include "spinel/spinel.h"
#include "spinel/spinel_assert.h"
#include "spinel/spinel_opcodes.h"
#include "tests/common/spinel/spinel_path_sink.h"
#include "tests/common/svg/svg_utils.h"
#include "tests/common/utils.h"  // For ASSERT_MSG()

// Set to 1 to enable debug logs.
#define DEBUG 0

#if DEBUG
#include <stdio.h>
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define LOG(...) ((void)0)
#endif

void
SvgSpinelImage::init(const svg * svg, spn_context_t context, const SpinelImage::Config & config)
{
  svg_ = svg;
  SpinelImage::init(context, config);
}

void
SvgSpinelImage::init(const svg * svg, spn_context_t context, uint32_t width, uint32_t height)
{
  svg_ = svg;
  SpinelImage::init(context, width, height);
}

void
SvgSpinelImage::init(const svg * svg, spn_context_t context)
{
  svg_ = svg;
  SpinelImage::init(context);
}

void
SvgSpinelImage::reset()
{
  resetLayers();
  resetRasters();
  resetPaths();
  SpinelImage::reset();
}

void
SvgSpinelImage::setupPaths()
{
  ASSERT_MSG(!paths_, "Cannot call setupPaths() twice without resetPaths()");

  SpinelPathSink spinel_paths(context);

  uint32_t path_count = svg_path_count(svg_);
  for (uint32_t nn = 0; nn < path_count; ++nn)
    svg_decode_path(svg_, nn, nullptr, &spinel_paths);

  std::vector<spn_path_t> paths = spinel_paths.release();
  ASSERT(paths.size() == path_count);

  // Copy from vector to heap-allocated array.
  paths_       = new spn_path_t[path_count];
  paths_count_ = path_count;
  for (uint32_t nn = 0; nn < path_count; ++nn)
    paths_[nn] = paths[nn];

  paths.clear();
}

void
SvgSpinelImage::resetPaths()
{
  if (paths_count_ > 0)
    {
      spn(path_release(context, paths_, paths_count_));
      delete[] paths_;
      paths_       = nullptr;
      paths_count_ = 0;
    }
}

void
SvgSpinelImage::setupRasters(const spn_transform_t * transform)
{
  ASSERT_MSG(!rasters_, "Cannot call setupRasters() twice without resetRasters()");

  // IMPORTANT: Some documents (e.g. insect.svg) have rasters that only
  // contain PathStroke commands, which are currently ignored. These rasters
  // will _not_ be enumerated by svg_decode_rasters, so initialize their path
  // handles with SPN_HANDLE_INVALID to be safe.
  uint32_t raster_count = svg_raster_count(svg_);

  rasters_       = new spn_raster_t[raster_count];
  rasters_count_ = raster_count;
  for (uint32_t nn = 0; nn < raster_count; ++nn)
    rasters_[nn] = SPN_RASTER_INVALID;

  if (transform)
    {
      transform_stack_push_matrix(transform_stack,
                                  transform->sx,
                                  transform->shx,
                                  transform->tx,
                                  transform->shy,
                                  transform->sy,
                                  transform->ty,
                                  transform->w0,
                                  transform->w1,
                                  1.0);
      transform_stack_concat(transform_stack);
    }

  auto callback = [&](const SvgDecodedRaster & r) -> bool {
    transform_stack_push_affine(transform_stack,
                                (float)r.transform.sx,
                                (float)r.transform.shx,
                                (float)r.transform.tx,
                                (float)r.transform.shy,
                                (float)r.transform.sy,
                                (float)r.transform.ty);

    transform_stack_concat(transform_stack);

    spn(raster_builder_begin(raster_builder));

    static const struct spn_clip raster_clips[] = { { 0., 0., FLT_MAX, FLT_MAX } };

    spn(raster_builder_add(raster_builder,
                           &paths_[r.path_id],
                           nullptr,  // transform_weakrefs
                           (const spn_transform_t *)transform_stack_top_transform(transform_stack),
                           nullptr,  // clip_weakrefs,
                           raster_clips,
                           1));

    ASSERT_MSG(r.raster_id < raster_count,
               "Invalid raster id=%u (should be < %u)\n",
               r.raster_id,
               raster_count);

    spn(raster_builder_end(raster_builder, &rasters_[r.raster_id]));

    LOG("raster_id:%u raster_handle:%u raster_count:%u\n",
        r.raster_id,
        rasters_[r.raster_id].handle,
        raster_count);

    transform_stack_drop(transform_stack);
    return true;
  };

  svg_decode_rasters(svg_, nullptr, callback);

  if (transform)
    transform_stack_drop(transform_stack);
}

void
SvgSpinelImage::resetRasters()
{
  if (rasters_count_ > 0)
    {
      // Remove SPN_RASTER_INVALID values from the |rasters_| array.
      uint32_t read_count  = 0;
      uint32_t write_count = 0;
      while (read_count < rasters_count_)
        {
          if (rasters_[read_count].handle != UINT32_MAX)
            {
              if (write_count < read_count)
                rasters_[write_count] = rasters_[read_count];
              write_count++;
            }
          read_count++;
        }

      spn(raster_release(context, rasters_, write_count));
      delete[] rasters_;
      rasters_       = nullptr;
      rasters_count_ = 0;
    }
}

void
SvgSpinelImage::setupLayers()
{
  //
  // Setup layers
  //
  bool is_srgb = false;

  uint32_t layer_count = svg_layer_count(svg_);

  // Create top-level styling group
  spn_group_id group_id;
  spn(styling_group_alloc(styling, &group_id));

  // This is the root group.
  spn(styling_group_parents(styling, group_id, 0, nullptr));

  // the range of the root group is maximal [0,layer_count)
  spn(styling_group_range_lo(styling, group_id, 0));
  spn(styling_group_range_hi(styling, group_id, layer_count - 1));

  {
    spn_styling_cmd_t * cmds;
    spn(styling_group_enter(styling, group_id, 1, &cmds));
    cmds[0] = SPN_STYLING_OPCODE_COLOR_ACC_ZERO;
  }

  {
    spn_styling_cmd_t * cmds;
    spn(styling_group_leave(styling, group_id, 4, &cmds));
    const float background[4] = { 1., 1., 1., 1. };
    spn_styling_background_over_encoder(cmds, background);
    cmds[3] = SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE;
  }

  uint32_t count    = 0;
  auto     callback = [&](const SvgDecodedLayer & l) -> bool {
    // Spinel renders front to back.
    spn_layer_id layer_id = layer_count - 1 - l.layer_id;

    float rgba[4];

    color_rgb32_to_rgba_f32(rgba, l.fill_color, l.fill_opacity);
    if (is_srgb)
      color_srgb_to_linear_rgb_f32(rgba);

    color_premultiply_rgba_f32(rgba);

    spn_styling_cmd_t * cmds;
    spn(styling_group_layer(styling, group_id, layer_id, 5, &cmds));
    cmds[0] = l.fill_even_odd ? SPN_STYLING_OPCODE_COVER_EVENODD : SPN_STYLING_OPCODE_COVER_NONZERO;
    spn_styling_layer_fill_rgba_encoder(cmds + 1, rgba);
    cmds[4] = SPN_STYLING_OPCODE_BLEND_OVER;

    for (const auto & print : l.prints)
      {
        // Ignore raster ids without a valid raster handle. This happens
        // when a layer references a raster with a PathStroke command.
        spn_raster_t raster = rasters_[print.raster_id];
        if (raster.handle == UINT32_MAX)
          continue;

        const spn_txty_t txty = {
          .tx = print.tx,
          .ty = print.ty,
        };

        LOG("layer_id:%u styling layer_id:%u raster_id:%u raster_handle:%u\n",
            l.layer_id,
            layer_id,
            print.raster_id,
            raster.handle);

        spn(composition_place(composition, &raster, &layer_id, &txty, 1));
      }

    count++;
    return true;
  };

  svg_decode_layers(svg_, callback);

  if (DEBUG)
    ASSERT_MSG(count == layer_count, "Invalid layer count %u (should be %u)\n", count, layer_count);

  spn(styling_seal(styling));
  spn(composition_seal(composition));
}

void
SvgSpinelImage::resetLayers()
{
  if (styling)
    {
      spn(styling_unseal(styling));
      spn(styling_reset(styling));
    }

  if (composition)
    {
      spn(composition_unseal(composition));
      spn(composition_reset(composition));
    }
}
