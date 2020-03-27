// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svg_scene_demo_image.h"

#include <utility>

#include "spinel/ext/color/color.h"
#include "spinel/ext/transform_stack/transform_stack.h"
#include "spinel/spinel.h"
#include "spinel/spinel_assert.h"
#include "spinel/spinel_opcodes.h"
#include "tests/common/spinel/spinel_path_sink.h"
#include "tests/common/svg/svg_utils.h"
#include "tests/common/utils.h"

#define DEBUG 0

#if DEBUG
#include <stdio.h>
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define LOG(...) ((void)0)
#endif

using FrameTransformFunc = SvgSceneDemoImage::FrameTransformFunc;

//
// SvgSceneDemoImage::Parent contains the shared state between
// all SvgSceneDemoImage instances. In this case, this corresponds
// to the SVG scene paths and Spinel builder handles.
//
class SvgSceneDemoImage::Parent {
 public:
  Parent(const SvgScene & scene, spn_context_t context, FrameTransformFunc transform_func)
      : scene_(scene), context_(context), transform_func_(transform_func)
  {
    // BUG: spn_context_retain() doesn't do anything, while spn_context_release()
    //      destroys the context immediately.
    //spn(context_retain(context));

    spn(path_builder_create(context, &path_builder_));
    spn(raster_builder_create(context, &raster_builder_));
  }

  ~Parent()
  {
    resetPaths();

    if (path_builder_)
      spn(path_builder_release(path_builder_));
    if (raster_builder_)
      spn(raster_builder_release(raster_builder_));

    // Don't release context here, see bug described in constructor.
    // spn(context_release(context_));
  }

 private:
  friend class SvgSceneDemoImage;

  void
  resetPaths()
  {
    spn(path_release(context_, paths_.data(), static_cast<uint32_t>(paths_.size())));
    paths_.clear();
  }

  const std::vector<spn_path_t> &
  getPaths()
  {
    // Regenerate paths if needed.
    if (scene_.ensureUpdated() || paths_.empty())
      {
        resetPaths();

        // Decode all paths into spn_path_t handles.
        SpinelPathSink spinel_paths(context_, path_builder_);

        const auto & svgs = scene_.unique_svgs();
        for (const SvgScene::Path & path : scene_.unique_paths())
          svg_decode_path(svgs[path.svg_index], path.path_id, nullptr, &spinel_paths);

        paths_ = spinel_paths.release();
      }

    return paths_;
  }

  const SvgScene &        scene_;
  spn_context_t           context_;
  FrameTransformFunc      transform_func_;
  spn_path_builder_t      path_builder_;
  spn_raster_builder_t    raster_builder_;
  std::vector<spn_path_t> paths_;
};

//
// SvgSceneDemoImage implementation.
//

SvgSceneDemoImage::SvgSceneDemoImage(SvgSceneDemoImage::Parent * parent,
                                     uint32_t                    clip_width,
                                     uint32_t                    clip_height)
    : parent_(parent), context_(parent_->context_)
{
  spn(composition_create(context_, &composition_));

  uint32_t clip[4] = { 0, 0, clip_width, clip_height };
  spn(composition_set_clip(composition_, clip));

  spn(styling_create(context_, &styling_, 4096, 16000));
}

SvgSceneDemoImage::~SvgSceneDemoImage()
{
  if (styling_)
    {
      spn(styling_unseal(styling_));
      spn(styling_release(styling_));
      styling_ = nullptr;
    }

  if (composition_)
    {
      spn(composition_unseal(composition_));
      spn(composition_release(composition_));
      composition_ = nullptr;
    }

  resetRasters();
}

void
SvgSceneDemoImage::resetRasters()
{
  spn(raster_release(context_, rasters_.data(), static_cast<uint32_t>(rasters_.size())));
  rasters_.clear();
}

void
SvgSceneDemoImage::resetLayers()
{
  if (styling_)
    {
      spn(styling_unseal(styling_));
      spn(styling_reset(styling_));
    }
  if (composition_)
    {
      spn(composition_unseal(composition_));
      spn(composition_reset(composition_));
    }
}

void
SvgSceneDemoImage::setup(uint32_t frame_counter)
{
  //
  // Setup rasters
  //
  spn_transform_t transform = parent_->transform_func_(frame_counter);

  const auto &                    scene_rasters = parent_->scene_.unique_rasters();
  const std::vector<spn_path_t> & spinel_paths  = parent_->getPaths();

  if (!scene_rasters.empty())
    {
      rasters_.reserve(scene_rasters.size());

      transform_stack * ts = transform_stack_create(2);

      // Scaling to Spinel's sub-pixel space first.
      transform_stack_push_scale(ts, 32., 32.);

      transform_stack_push_matrix(ts,
                                  transform.sx,
                                  transform.shx,
                                  transform.tx,
                                  transform.shy,
                                  transform.sy,
                                  transform.ty,
                                  transform.w0,
                                  transform.w1,
                                  1.);
      transform_stack_concat(ts);

      spn_raster_builder_t raster_builder = parent_->raster_builder_;

      for (const SvgScene::Raster & raster : scene_rasters)
        {
          static const struct spn_clip raster_clips[] = { { 0., 0., FLT_MAX, FLT_MAX } };
          spn_raster_t                 handle;

          transform_stack_push_affine(ts,
                                      (float)raster.transform.sx,
                                      (float)raster.transform.shx,
                                      (float)raster.transform.tx,
                                      (float)raster.transform.shy,
                                      (float)raster.transform.sy,
                                      (float)raster.transform.ty);
          transform_stack_concat(ts);

          spn(raster_builder_begin(raster_builder));

          spn(raster_builder_add(raster_builder,
                                 &spinel_paths[raster.path_index],
                                 nullptr,  // transform_weakrefs
                                 (const spn_transform_t *)transform_stack_top_transform(ts),
                                 nullptr,  // clip_weakrefs
                                 raster_clips,
                                 1));

          spn(raster_builder_end(raster_builder, &handle));

          rasters_.push_back(handle);

          transform_stack_drop(ts);
        }

      transform_stack_release(ts);
    }

  //
  // Setup layers
  //
  bool is_srgb = false;

  // First thing to do is setup the composition.
  const auto & scene_layers = parent_->scene_.layers();

  uint32_t layer_count = static_cast<uint32_t>(scene_layers.size());

  // Create top-level styling group
  spn_group_id group_id;
  spn(styling_group_alloc(styling_, &group_id));

  // This is the root group.
  spn(styling_group_parents(styling_, group_id, 0, nullptr));

  // the range of the root group is maximal [0,layer_count)
  spn(styling_group_range_lo(styling_, group_id, 0));
  spn(styling_group_range_hi(styling_, group_id, layer_count - 1));

  {
    spn_styling_cmd_t * cmds;
    spn(styling_group_enter(styling_, group_id, 1, &cmds));
    cmds[0] = SPN_STYLING_OPCODE_COLOR_ACC_ZERO;
  }

  {
    spn_styling_cmd_t * cmds;
    spn(styling_group_leave(styling_, group_id, 4, &cmds));
    const float background[4] = { 1., 1., 1., 1. };
    spn_styling_background_over_encoder(cmds, background);
    cmds[3] = SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE;
  }

  LOG("------\n");
  for (const auto & l : scene_layers)
    {
      // Spinel renders front to back.
      spn_layer_id layer_id = layer_count - 1 - l.layer_id;

      LOG("layer_id:%u l.layer_id:%u fill_color=%08x fill_opacity=%g\n",
          layer_id,
          l.layer_id,
          l.fill_color,
          l.fill_opacity);

      {
        float rgba[4];

        color_rgb32_to_rgba_f32(rgba, l.fill_color, l.fill_opacity);
        if (is_srgb)
          color_srgb_to_linear_rgb_f32(rgba);

        color_premultiply_rgba_f32(rgba);

        spn_styling_cmd_t * cmds;
        spn(styling_group_layer(styling_, group_id, layer_id, 5, &cmds));
        cmds[0] =
          l.fill_even_odd ? SPN_STYLING_OPCODE_COVER_EVENODD : SPN_STYLING_OPCODE_COVER_NONZERO;
        spn_styling_layer_fill_rgba_encoder(cmds + 1, rgba);
        cmds[4] = SPN_STYLING_OPCODE_BLEND_OVER;
      }

      for (const auto & print : l.prints)
        {
          const spn_txty_t txty = {
            .tx = print.tx,
            .ty = print.ty,
          };
          spn(composition_place(composition_, &rasters_[print.raster_index], &layer_id, &txty, 1));
          LOG("  %u raster_id:%u\n", layer_id, print.raster_index);
        }
    }

  spn(composition_seal(composition_));
  spn(styling_seal(styling_));
}

void
SvgSceneDemoImage::render(void * submit_ext, uint32_t width, uint32_t height)
{
  const spn_render_submit_t submit = {
    .ext         = submit_ext,
    .styling     = styling_,
    .composition = composition_,
    .clip        = { 0, 0, width, height },
  };
  spn(render(context_, &submit));

  // Discard rasters now.
  resetRasters();
}

void
SvgSceneDemoImage::flush()
{
  resetLayers();
}

// static
DemoImage::Factory
SvgSceneDemoImage::makeFactory(const SvgScene & scene, FrameTransformFunc transform_func)
{
  // Create a lambda that creates the parent instance on the first call.
  std::shared_ptr<SvgSceneDemoImage::Parent> parent;

  return [parent, transform_func, &scene](const DemoImage::Config & config) mutable {
    if (!parent.get())
      parent = std::make_shared<SvgSceneDemoImage::Parent>(scene, config.context, transform_func);

    return std::make_unique<SvgSceneDemoImage>(parent.get(),
                                               config.surface_width,
                                               config.surface_height);
  };
}
