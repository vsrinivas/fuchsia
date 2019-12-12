// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock_spinel.h"

#include <float.h>
#include <limits.h>

#include "spinel/spinel_opcodes.h"

namespace mock_spinel {

///
///  Context
///

const Path *
Context::pathFor(spn_path_t handle)
{
  if (handle.handle < paths_.size())
    return &paths_[handle.handle];
  else
    return nullptr;
}

const Raster *
Context::rasterFor(spn_raster_t handle)
{
  if (handle.handle < rasters_.size())
    return &rasters_[handle.handle];
  else
    return nullptr;
}

spn_result_t
Context::reset()
{
  paths_.clear();
  rasters_.clear();
  return SPN_SUCCESS;
}

spn_result_t
Context::status() const
{
  return SPN_SUCCESS;
}

spn_result_t
Context::createPathBuilder(spn_path_builder_t * path_builder)
{
  PathBuilder * builder = new PathBuilder(this);
  *path_builder         = builder->toSpinel();
  return SPN_SUCCESS;
}

spn_result_t
Context::createRasterBuilder(spn_raster_builder_t * raster_builder)
{
  RasterBuilder * builder = new RasterBuilder(this);
  *raster_builder         = builder->toSpinel();
  return SPN_SUCCESS;
}

spn_result_t
Context::createComposition(spn_composition_t * composition)
{
  Composition * comp = new Composition(this);
  *composition       = comp->toSpinel();
  return SPN_SUCCESS;
}

spn_result_t
Context::cloneComposition(spn_composition_t composition, spn_composition_t * clone)
{
  Composition * copy = Composition::fromSpinel(composition)->clone();
  *clone             = copy->toSpinel();
  return SPN_SUCCESS;
}

spn_result_t
Context::createStyling(uint32_t layers_count, uint32_t cmds_count, spn_styling_t * styling)
{
  Styling * styl = new Styling(this, layers_count, cmds_count);
  *styling       = styl->toSpinel();
  return SPN_SUCCESS;
}

spn_result_t
Context::retainPaths(const spn_path_t * ids, uint32_t count)
{
  return SPN_SUCCESS;
}

spn_result_t
Context::releasePaths(const spn_path_t * ids, uint32_t count)
{
  return SPN_SUCCESS;
}

spn_result_t
Context::retainRasters(const spn_raster_t * ids, uint32_t count)
{
  return SPN_SUCCESS;
}

spn_result_t
Context::releaseRasters(const spn_raster_t * ids, uint32_t count)
{
  return SPN_SUCCESS;
}

spn_result_t
Context::render(const spn_render_submit_t *)
{
  ASSERT_MSG(false, "Unimplemented");
  return SPN_ERROR_CONTEXT_LOST;
}

// Called from the path and raster builders to add a new path or raster instance
// and return its Spinel handle.
spn_path_t
Context::installPath(Path && path)
{
  uint32_t handle = static_cast<uint32_t>(paths_.size());
  paths_.push_back(std::move(path));
  return { handle };
}

spn_raster_t
Context::installRaster(Raster && raster)
{
  uint32_t handle = static_cast<uint32_t>(rasters_.size());
  rasters_.push_back(std::move(raster));
  return { handle };
}

///
///  Path builder
///

spn_result_t
PathBuilder::flush()
{
  return SPN_SUCCESS;
}

spn_result_t
PathBuilder::begin()
{
  return reset();
}

spn_result_t
PathBuilder::end(spn_path_t * path)
{
  *path = context_->installPath(std::move(path_));
  return SPN_SUCCESS;
}

spn_result_t
PathBuilder::reset()
{
  path_.data.clear();
  return SPN_SUCCESS;
}

spn_result_t
PathBuilder::moveTo(float x, float y)
{
  Path::MoveTo element = {
    .x = x,
    .y = y,
  };
  path_.add(element);
  return SPN_SUCCESS;
}

spn_result_t
PathBuilder::lineTo(float x, float y)
{
  Path::LineTo element = {
    .x = x,
    .y = y,
  };
  path_.add(element);
  return SPN_SUCCESS;
}

spn_result_t
PathBuilder::quadTo(float cx, float cy, float x, float y)
{
  Path::QuadTo element = {
    .cx = cx,
    .cy = cy,
    .x  = x,
    .y  = y,
  };
  path_.add(element);
  return SPN_SUCCESS;
}

spn_result_t
PathBuilder::cubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y)
{
  Path::CubicTo element = {
    .c1x = c1x,
    .c1y = c1y,
    .c2x = c2x,
    .c2y = c2y,
    .x   = x,
    .y   = y,
  };
  path_.add(element);
  return SPN_SUCCESS;
}

spn_result_t
PathBuilder::ratQuadTo(float cx, float cy, float x, float y, float w)
{
  Path::RatQuadTo element = {
    .cx = cx,
    .cy = cy,
    .x  = x,
    .y  = y,
    .w  = w,
  };
  path_.add(element);
  return SPN_SUCCESS;
}

spn_result_t
PathBuilder::ratCubicTo(
  float c1x, float c1y, float c2x, float c2y, float x, float y, float w1, float w2)
{
  Path::RatCubicTo element = {
    .c1x = c1x,
    .c1y = c1y,
    .c2x = c2x,
    .c2y = c2y,
    .x   = x,
    .y   = y,
    .w1  = w1,
    .w2  = w2,
  };
  path_.add(element);
  return SPN_SUCCESS;
}

///
///  Raster builder
///

spn_result_t
RasterBuilder::flush()
{
  return SPN_SUCCESS;
}

spn_result_t
RasterBuilder::begin()
{
  raster_.clear();
  return SPN_SUCCESS;
}

spn_result_t
RasterBuilder::end(spn_raster_t * raster)
{
  *raster = context_->installRaster(std::move(raster_));
  raster_.clear();
  return SPN_SUCCESS;
}

spn_result_t
RasterBuilder::add(spn_path_t const *        paths,
                   spn_transform_weakref_t * transform_weakrefs,
                   spn_transform_t const *   transforms,
                   spn_clip_weakref_t *      clip_weakrefs,
                   spn_clip_t const *        clips,
                   uint32_t                  count)
{
  for (uint32_t nn = 0; nn < count; ++nn)
    {
      RasterPath rpath = {
        .path_id   = paths[nn].handle,
        .transform = { 1., 0., 0., 0., 1., 0., 0., 0. },  // identity
        .clip      = { 0., 0., FLT_MAX, FLT_MAX },
      };
      if (transforms)
        rpath.transform = transforms[nn];
      if (clips)
        rpath.clip = clips[nn];
      raster_.push_back(std::move(rpath));
    }
  return SPN_SUCCESS;
}

///
///  Composition
///

Composition *
Composition::clone()
{
  Composition * result = new Composition(context_);
  result->prints_.assign(prints_.begin(), prints_.end());
  return result;
}

spn_result_t
Composition::place(spn_raster_t const * rasters,
                   spn_layer_id const * layer_ids,
                   spn_txty_t const *   txtys,
                   uint32_t             count)
{
  for (uint32_t nn = 0; nn < count; ++nn)
    {
      RasterPrint print = {
        .raster_id   = rasters[nn].handle,
        .layer_id    = layer_ids[nn],
        .translation = { 0, 0 },
      };
      if (txtys)
        print.translation = txtys[nn];

      prints_.push_back(std::move(print));
    }
  return SPN_SUCCESS;
}

spn_result_t
Composition::seal()
{
  return SPN_SUCCESS;
}

Composition::LayerMap
Composition::computeLayerMap() const
{
  Composition::LayerMap result;
  for (const RasterPrint & print : prints_)
    result[print.layer_id].push_back(&print);

  return result;
}

spn_result_t
Composition::unseal()
{
  return SPN_SUCCESS;
}

spn_result_t
Composition::reset()
{
  prints_.clear();
  return SPN_SUCCESS;
}

spn_result_t
Composition::getBounds(uint32_t bounds[4]) const
{
  ASSERT_MSG(false, "Unimplemented");
  return SPN_ERROR_CONTEXT_LOST;
}

spn_result_t
Composition::setClip(const uint32_t clip[4])
{
  // Ignored for now.
  return SPN_SUCCESS;
}

///
///  Styling
///

spn_result_t
Styling::seal()
{
  return SPN_SUCCESS;
}

spn_result_t
Styling::unseal()
{
  return SPN_SUCCESS;
}

spn_result_t
Styling::reset()
{
  groups_.clear();
  return SPN_SUCCESS;
}

spn_result_t
Styling::groupAllocId(spn_group_id * group_id)
{
  *group_id = groups_.size();
  groups_.push_back(StylingGroup());
  return SPN_SUCCESS;
}

spn_result_t
Styling::groupAllocEnterCommands(spn_group_id group_id, uint32_t count, spn_styling_cmd_t ** cmds)
{
  ASSERT_MSG(group_id < groups_.size(), "Invalid group id");
  StylingCommands & commands = groups_[group_id].begin_commands;
  commands.resize(count);
  if (cmds)
    {
      *cmds = count ? commands.data() : nullptr;
    }
  return SPN_SUCCESS;
}

spn_result_t
Styling::groupAllocLeaveCommands(spn_group_id group_id, uint32_t count, spn_styling_cmd_t ** cmds)
{
  ASSERT_MSG(group_id < groups_.size(), "Invalid group id");
  StylingCommands & commands = groups_[group_id].end_commands;
  commands.resize(count);
  if (cmds)
    {
      *cmds = count ? commands.data() : nullptr;
    }
  return SPN_SUCCESS;
}

spn_result_t
Styling::groupAllocLayerCommands(spn_group_id         group_id,
                                 spn_layer_id         layer_id,
                                 uint32_t             count,
                                 spn_styling_cmd_t ** cmds)
{
  ASSERT(group_id < groups_.size());
  StylingCommands & commands = groups_[group_id].layer_commands[layer_id];
  commands.resize(count);
  if (cmds)
    {
      *cmds = count ? commands.data() : nullptr;
    }
  return SPN_SUCCESS;
}

spn_result_t
Styling::groupAllocParents(spn_group_id group_id, uint32_t count, spn_group_id ** parents)
{
  // We don't deal with parents for now because our demos don't use them.
  ASSERT_MSG(count == 0, "TODO: Implement styling group parents");
  return SPN_SUCCESS;
}

spn_result_t
Styling::groupSetRangeLo(spn_group_id group_id, spn_layer_id layer_lo)
{
  ASSERT(group_id < groups_.size());
  groups_[group_id].layer_lo = layer_lo;
  return SPN_SUCCESS;
}

spn_result_t
Styling::groupSetRangeHi(spn_group_id group_id, spn_layer_id layer_hi)
{
  ASSERT(group_id < groups_.size());
  groups_[group_id].layer_hi = layer_hi;
  return SPN_SUCCESS;
}

//
// Spinel
//

void
Spinel::rgbaToCmds(const float rgba[4], spn_styling_cmd_t cmds[2])
{
  uint8_t r8 = (uint8_t)(255 * rgba[0]);
  uint8_t g8 = (uint8_t)(255 * rgba[1]);
  uint8_t b8 = (uint8_t)(255 * rgba[2]);
  uint8_t a8 = (uint8_t)(255 * rgba[3]);
  cmds[0]    = ((uint32_t)r8 << 24) | ((uint32_t)g8 << 16) | ((uint32_t)b8 << 8) | a8;
  cmds[1]    = 0;
}

void
Spinel::cmdsToRgba(const spn_styling_cmd_t cmds[2], float rgba[4])
{
  uint8_t r8 = (uint8_t)(cmds[0] >> 24);
  uint8_t g8 = (uint8_t)(cmds[0] >> 16);
  uint8_t b8 = (uint8_t)(cmds[0] >> 8);
  uint8_t a8 = (uint8_t)(cmds[0] >> 0);
  rgba[0]    = r8 / 255.;
  rgba[1]    = g8 / 255.;
  rgba[2]    = b8 / 255.;
  rgba[3]    = a8 / 255.;
}

void
spn_styling_layer_fill_rgba_encoder(spn_styling_cmd_t * cmds, float const rgba[4])
{
  cmds[0] = SPN_STYLING_OPCODE_COLOR_FILL_SOLID;
  Spinel::rgbaToCmds(rgba, cmds + 1);
}

void
Spinel::encodeCommandFillRgba(spn_styling_cmd_t * cmds, const float rgba[4])
{
  cmds[0] = SPN_STYLING_OPCODE_COLOR_FILL_SOLID;
  Spinel::rgbaToCmds(rgba, cmds + 1);
}

void
Spinel::encodeCommandBackgroundOver(spn_styling_cmd_t * cmds, float const rgba[4])
{
  cmds[0] = SPN_STYLING_OPCODE_COLOR_ACC_OVER_BACKGROUND;
  Spinel::rgbaToCmds(rgba, cmds + 1);
}

spn_result_t
Spinel::createContext(spn_context_t * context)
{
  Context * c = new Context();
  *context    = c->toSpinel();
  return SPN_SUCCESS;
}

}  // namespace mock_spinel
