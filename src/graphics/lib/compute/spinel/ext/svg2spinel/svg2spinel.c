// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spinel/ext/svg2spinel/svg2spinel.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spinel/ext/color/color.h"
#include "spinel/ext/geometry/ellipse.h"
#include "spinel/ext/geometry/svg_arc.h"
#include "spinel/spinel_assert.h"
#include "spinel/spinel_opcodes.h"

//
//
//

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//
//
//

void
spinel_svg_paths_release(const struct svg * const svg,
                         spinel_context_t         context,
                         spinel_path_t * const    paths)
{
  uint32_t const n = svg_path_count(svg);

  spinel(path_release(context, paths, n));

  free(paths);
}

void
spinel_svg_rasters_release(const struct svg * const svg,
                           spinel_context_t         context,
                           spinel_raster_t * const  rasters)

{
  uint32_t const n = svg_raster_count(svg);

  spinel(raster_release(context, rasters, n));

  free(rasters);
}

//
//
//

static void
spinel_svg_poly_read(struct svg_path_iterator * iter, spinel_path_builder_t pb, bool const close)
{
  union svg_path_cmd const * cmd;
  float                      x0, y0;
  bool                       lineto = false;

  while (svg_path_iterator_next(iter, &cmd))
    {
      if (cmd->type != SVG_PATH_CMD_POLY_POINT)
        break;

      if (lineto)
        {
          spinel(path_builder_line_to(pb, cmd->poly_point.x, cmd->poly_point.y));
        }
      else
        {
          spinel(path_builder_move_to(pb, x0 = cmd->poly_point.x, y0 = cmd->poly_point.y));
          lineto = true;
        }
    }

  if (close && lineto)
    {
      spinel(path_builder_line_to(pb, x0, y0));
    }
}

//
//
//

static void
spinel_svg_implicit_close_filled_path(
  spinel_path_builder_t pb, float x0, float y0, float x, float y)
{
  if ((x != x0) || (y != y0))
    spinel(path_builder_line_to(pb, x0, y0));
}

//
//
//

spinel_path_t *
spinel_svg_paths_decode(struct svg const * const svg, spinel_path_builder_t pb)
{
  spinel_path_t * const paths = malloc(sizeof(*paths) * svg_path_count(svg));

  union svg_path_cmd const * cmd;

  float x0, y0, x, y;

  struct svg_path_iterator * iter = svg_path_iterator_create(svg, UINT32_MAX);

  while (svg_path_iterator_next(iter, &cmd))
    {
      switch (cmd->type)
        {
          case SVG_PATH_CMD_BEGIN:
            spinel(path_builder_begin(pb));
            break;

          case SVG_PATH_CMD_END:
            spinel(path_builder_end(pb, paths + cmd->end.path_index));
            break;

          case SVG_PATH_CMD_CIRCLE:
            spinel(path_builder_ellipse(pb,
                                        cmd->circle.cx,
                                        cmd->circle.cy,
                                        cmd->circle.r,
                                        cmd->circle.r));
            break;

          case SVG_PATH_CMD_ELLIPSE:
            spinel(path_builder_ellipse(pb,
                                        cmd->ellipse.cx,
                                        cmd->ellipse.cy,
                                        cmd->ellipse.rx,
                                        cmd->ellipse.ry));
            break;

          case SVG_PATH_CMD_LINE:
            spinel(path_builder_move_to(pb, cmd->line.x1, cmd->line.y1));
            spinel(path_builder_line_to(pb, cmd->line.x2, cmd->line.y2));
            break;

          case SVG_PATH_CMD_POLYGON:
            spinel_svg_poly_read(iter, pb, true);
            break;

          case SVG_PATH_CMD_POLYLINE:
            spinel_svg_poly_read(iter, pb, false);
            break;

          case SVG_PATH_CMD_RECT:
            spinel(path_builder_move_to(pb, cmd->rect.x, cmd->rect.y));
            spinel(path_builder_line_to(pb, cmd->rect.x + cmd->rect.width, cmd->rect.y));
            spinel(path_builder_line_to(pb,
                                        cmd->rect.x + cmd->rect.width,
                                        cmd->rect.y + cmd->rect.height));
            spinel(path_builder_line_to(pb, cmd->rect.x, cmd->rect.y + cmd->rect.height));
            spinel(path_builder_line_to(pb, cmd->rect.x, cmd->rect.y));
            break;

          case SVG_PATH_CMD_PATH_BEGIN:
            x = x0 = 0.0f;
            y = y0 = 0.0f;
            break;

          case SVG_PATH_CMD_PATH_END:
            spinel_svg_implicit_close_filled_path(pb, x0, y0, x, y);
            break;

          case SVG_PATH_CMD_MOVE_TO:
            spinel_svg_implicit_close_filled_path(pb, x0, y0, x, y);
            spinel_path_builder_move_to(pb, x0 = x = cmd->move_to.x, y0 = y = cmd->move_to.y);
            break;

          case SVG_PATH_CMD_MOVE_TO_REL:
            spinel_svg_implicit_close_filled_path(pb, x0, y0, x, y);
            spinel(
              path_builder_move_to(pb, x0 = (x += cmd->move_to.x), y0 = (y += cmd->move_to.y)));
            break;

          case SVG_PATH_CMD_CLOSE_UPPER:
          case SVG_PATH_CMD_CLOSE:
            spinel_svg_implicit_close_filled_path(pb, x0, y0, x, y);
            spinel(path_builder_move_to(pb, x = x0, y = y0));  // reset to initial point
            break;

          case SVG_PATH_CMD_LINE_TO:
            spinel(path_builder_line_to(pb, x = cmd->line_to.x, y = cmd->line_to.y));
            break;

          case SVG_PATH_CMD_LINE_TO_REL:
            spinel(path_builder_line_to(pb, x += cmd->line_to.x, y += cmd->line_to.y));
            break;

          case SVG_PATH_CMD_HLINE_TO:
            spinel(path_builder_line_to(pb, x = cmd->hline_to.c, y));
            break;

          case SVG_PATH_CMD_HLINE_TO_REL:
            spinel(path_builder_line_to(pb, x += cmd->hline_to.c, y));
            break;

          case SVG_PATH_CMD_VLINE_TO:
            spinel(path_builder_line_to(pb, x, y = cmd->vline_to.c));
            break;

          case SVG_PATH_CMD_VLINE_TO_REL:
            spinel(path_builder_line_to(pb, x, y += cmd->vline_to.c));
            break;

          case SVG_PATH_CMD_CUBIC_TO:
            spinel(path_builder_cubic_to(pb,
                                         cmd->cubic_to.x1,
                                         cmd->cubic_to.y1,
                                         cmd->cubic_to.x2,
                                         cmd->cubic_to.y2,
                                         x = cmd->cubic_to.x,
                                         y = cmd->cubic_to.y));
            break;

          case SVG_PATH_CMD_CUBIC_TO_REL:
            spinel(path_builder_cubic_to(pb,
                                         x + cmd->cubic_to.x1,
                                         y + cmd->cubic_to.y1,
                                         x + cmd->cubic_to.x2,
                                         y + cmd->cubic_to.y2,
                                         x + cmd->cubic_to.x,
                                         y + cmd->cubic_to.y));
            x += cmd->cubic_to.x;
            y += cmd->cubic_to.y;
            break;

          case SVG_PATH_CMD_CUBIC_SMOOTH_TO:
            spinel(path_builder_cubic_smooth_to(pb,
                                                cmd->cubic_smooth_to.x2,
                                                cmd->cubic_smooth_to.y2,
                                                x = cmd->cubic_smooth_to.x,
                                                y = cmd->cubic_smooth_to.y));
            break;

          case SVG_PATH_CMD_CUBIC_SMOOTH_TO_REL:
            spinel(path_builder_cubic_smooth_to(pb,
                                                x + cmd->cubic_smooth_to.x2,
                                                y + cmd->cubic_smooth_to.y2,
                                                x + cmd->cubic_smooth_to.x,
                                                y + cmd->cubic_smooth_to.y));
            x += cmd->cubic_smooth_to.x;
            y += cmd->cubic_smooth_to.y;
            break;

          case SVG_PATH_CMD_QUAD_TO:
            spinel(path_builder_quad_to(pb,
                                        cmd->quad_to.x1,
                                        cmd->quad_to.y1,
                                        x = cmd->quad_to.x,
                                        y = cmd->quad_to.y));
            break;

          case SVG_PATH_CMD_QUAD_TO_REL:
            spinel(path_builder_quad_to(pb,
                                        x + cmd->quad_to.x1,
                                        y + cmd->quad_to.y1,
                                        x + cmd->quad_to.x,
                                        y + cmd->quad_to.y));
            x += cmd->quad_to.x;
            y += cmd->quad_to.y;
            break;

          case SVG_PATH_CMD_QUAD_SMOOTH_TO:
            spinel(path_builder_quad_smooth_to(pb,
                                               x = cmd->quad_smooth_to.x,
                                               y = cmd->quad_smooth_to.y));
            break;

          case SVG_PATH_CMD_QUAD_SMOOTH_TO_REL:
            spinel(path_builder_quad_smooth_to(pb,
                                               x + cmd->quad_smooth_to.x,
                                               y + cmd->quad_smooth_to.y));
            x += cmd->quad_smooth_to.x;
            y += cmd->quad_smooth_to.y;
            break;

          case SVG_PATH_CMD_RAT_CUBIC_TO:
            spinel(path_builder_rat_cubic_to(pb,
                                             cmd->rat_cubic_to.x1,
                                             cmd->rat_cubic_to.y1,
                                             cmd->rat_cubic_to.x2,
                                             cmd->rat_cubic_to.y2,
                                             x = cmd->rat_cubic_to.x,
                                             y = cmd->rat_cubic_to.y,
                                             cmd->rat_cubic_to.w1,
                                             cmd->rat_cubic_to.w2));
            break;

          case SVG_PATH_CMD_RAT_CUBIC_TO_REL:
            spinel(path_builder_rat_cubic_to(pb,
                                             x + cmd->rat_cubic_to.x1,
                                             y + cmd->rat_cubic_to.y1,
                                             x + cmd->rat_cubic_to.x2,
                                             y + cmd->rat_cubic_to.y2,
                                             x + cmd->rat_cubic_to.x,
                                             y + cmd->rat_cubic_to.y,
                                             cmd->rat_cubic_to.w1,
                                             cmd->rat_cubic_to.w2));
            x += cmd->rat_cubic_to.x;
            y += cmd->rat_cubic_to.y;
            break;

          case SVG_PATH_CMD_RAT_QUAD_TO:
            spinel(path_builder_rat_quad_to(pb,
                                            cmd->rat_quad_to.x1,
                                            cmd->rat_quad_to.y1,
                                            x = cmd->rat_quad_to.x,
                                            y = cmd->rat_quad_to.y,
                                            cmd->rat_quad_to.w1));
            break;

          case SVG_PATH_CMD_RAT_QUAD_TO_REL:
            spinel(path_builder_rat_quad_to(pb,
                                            x + cmd->rat_quad_to.x1,
                                            y + cmd->rat_quad_to.y1,
                                            x + cmd->rat_quad_to.x,
                                            y + cmd->rat_quad_to.y,
                                            cmd->rat_quad_to.w1));
            x += cmd->rat_quad_to.x;
            y += cmd->rat_quad_to.y;
            break;

          case SVG_PATH_CMD_ARC_TO:
            // clang-format off
            {
              struct spinel_arc_params arc_params;

              spinel_svg_arc(x,
                          y,
                          cmd->arc_to.x,
                          cmd->arc_to.y,
                          cmd->arc_to.rx,
                          cmd->arc_to.ry,
                          cmd->arc_to.x_axis_rotation * (float)(M_PI / 180.0),
                          cmd->arc_to.large_arc_flag != 0.0f,
                          cmd->arc_to.sweep_flag != 0.0f,
                          &arc_params);

              spinel_path_builder_arc(pb, &arc_params);

              x = cmd->arc_to.x;
              y = cmd->arc_to.y;
            }
            // clang-format on
            break;

          case SVG_PATH_CMD_ARC_TO_REL:
            // clang-format off
            {
              struct spinel_arc_params arc_params;

              float x1 = x + cmd->arc_to.x;
              float y1 = y + cmd->arc_to.y;

              spinel_svg_arc(x,
                          y,
                          x1,
                          y1,
                          cmd->arc_to.rx,
                          cmd->arc_to.ry,
                          cmd->arc_to.x_axis_rotation * (float)(M_PI / 180.0),
                          cmd->arc_to.large_arc_flag != 0.0f,
                          cmd->arc_to.sweep_flag != 0.0f,
                          &arc_params);

              x = x1;
              y = y1;

              spinel_path_builder_arc(pb, &arc_params);
            }
            // clang-format on
            break;

          default:
            fprintf(stderr, "Error: unhandled path type - %u\n", cmd->type);
            exit(EXIT_FAILURE);
        }
    }

  svg_path_iterator_dispose(iter);

  return paths;
}

//
//
//

spinel_raster_t *
spinel_svg_rasters_decode(struct svg const * const              svg,
                          spinel_raster_builder_t               rb,
                          spinel_path_t const * const           paths,
                          struct spinel_transform_stack * const ts)
{
  static struct spinel_clip const raster_clips[] = { { 0.0f, 0.0f, FLT_MAX, FLT_MAX } };

  spinel_raster_t * const rasters    = malloc(sizeof(*rasters) * svg_raster_count(svg));
  uint32_t const          ts_restore = spinel_transform_stack_save(ts);

  union svg_raster_cmd const * cmd;

  struct svg_raster_iterator * iter = svg_raster_iterator_create(svg, UINT32_MAX);

  while (svg_raster_iterator_next(iter, &cmd))
    {
      switch (cmd->type)
        {
          case SVG_RASTER_CMD_BEGIN:
            spinel(raster_builder_begin(rb));
            break;

          case SVG_RASTER_CMD_END:
            spinel(raster_builder_end(rb, rasters + cmd->end.raster_index));
            break;

          case SVG_RASTER_CMD_FILL:
            spinel(
              raster_builder_add(rb,
                                 paths + cmd->fill.path_index,
                                 NULL,  // spinel_transform_stack_top_weakref(ts),
                                 (spinel_transform_t *)spinel_transform_stack_top_transform(ts),
                                 NULL,
                                 raster_clips,
                                 1));
            break;

          case SVG_RASTER_CMD_STROKE:  // FIXME(allanmac): IGNORED
            break;

          case SVG_RASTER_CMD_MARKER:  // FIXME(allanmac): IGNORED
            break;

          case SVG_RASTER_CMD_STROKE_WIDTH:  // FIXME(allanmac): IGNORED
            break;

          case SVG_RASTER_CMD_TRANSFORM_PROJECT:
            spinel_transform_stack_push_matrix(ts,
                                               cmd->project.sx,
                                               cmd->project.shx,
                                               cmd->project.tx,
                                               cmd->project.shy,
                                               cmd->project.sy,
                                               cmd->project.ty,
                                               cmd->project.w0,
                                               cmd->project.w1,
                                               1);
            spinel_transform_stack_concat(ts);
            break;

          case SVG_RASTER_CMD_TRANSFORM_MATRIX:
            spinel_transform_stack_push_affine(ts,
                                               cmd->matrix.sx,
                                               cmd->matrix.shx,
                                               cmd->matrix.tx,
                                               cmd->matrix.shy,
                                               cmd->matrix.sy,
                                               cmd->matrix.ty);
            spinel_transform_stack_concat(ts);
            break;

          case SVG_RASTER_CMD_TRANSFORM_TRANSLATE:
            spinel_transform_stack_push_translate(ts, cmd->translate.tx, cmd->translate.ty);
            spinel_transform_stack_concat(ts);
            break;

          case SVG_RASTER_CMD_TRANSFORM_SCALE:
            spinel_transform_stack_push_scale(ts, cmd->scale.sx, cmd->scale.sy);
            spinel_transform_stack_concat(ts);
            break;

          case SVG_RASTER_CMD_TRANSFORM_ROTATE:
            spinel_transform_stack_push_rotate_xy(ts,
                                                  cmd->rotate.d * (float)(M_PI / 180.0),
                                                  cmd->rotate.cx,
                                                  cmd->rotate.cy);
            spinel_transform_stack_concat(ts);
            break;

          case SVG_RASTER_CMD_TRANSFORM_SKEW_X:
            spinel_transform_stack_push_skew_x(ts, cmd->skew_x.d * (float)(M_PI / 180.0));
            spinel_transform_stack_concat(ts);
            break;

          case SVG_RASTER_CMD_TRANSFORM_SKEW_Y:
            spinel_transform_stack_push_skew_y(ts, cmd->skew_y.d * (float)(M_PI / 180.0));
            spinel_transform_stack_concat(ts);
            break;

          case SVG_RASTER_CMD_TRANSFORM_DROP:
            spinel_transform_stack_drop(ts);
            break;
        }
    }

  // restore stack depth
  spinel_transform_stack_restore(ts, ts_restore);

  svg_raster_iterator_dispose(iter);

  return rasters;
}

//
//
//

void
spinel_svg_layers_decode_at(spinel_layer_id const         layer_base,
                            spinel_group_id const         group_id,
                            struct svg const * const      svg,
                            spinel_raster_t const * const rasters,
                            spinel_composition_t          composition,
                            spinel_styling_t              styling,
                            bool const                    is_srgb)
{
  //
  // decode the svg
  //
  union svg_layer_cmd const * cmd;

  spinel_layer_id layer_id;

  spinel_styling_cmd_t fill_rule  = SPN_STYLING_OPCODE_COVER_NONZERO;
  spinel_styling_cmd_t blend_mode = SPN_STYLING_OPCODE_BLEND_OVER;

  svg_color_t rgb          = 0;
  float       opacity      = 1.0f;
  float       fill_opacity = 1.0f;

  struct svg_layer_iterator * iter = svg_layer_iterator_create(svg, UINT32_MAX);

  while (svg_layer_iterator_next(iter, &cmd))
    {
      switch (cmd->type)
        {
            case SVG_LAYER_CMD_BEGIN: {
              // this demo renders front to back
              layer_id = layer_base + svg_layer_count(svg) - 1 - cmd->begin.layer_index;
            }
            break;

            case SVG_LAYER_CMD_END: {
              float rgba[4];

              color_rgb32_to_rgba_f32(rgba, rgb, fill_opacity * opacity);

              if (is_srgb)
                {
                  color_srgb_to_linear_rgb_f32(rgba);
                }

              color_premultiply_rgba_f32(rgba);

              spinel_styling_cmd_t * cmds;

#define spinel_svg2SPINEL_DISABLE_OPACITY
#ifndef spinel_svg2SPINEL_DISABLE_OPACITY
              spinel(styling_group_layer(styling, group_id, layer_id, 6, &cmds));
#else
              spinel(styling_group_layer(styling, group_id, layer_id, 5, &cmds));
#endif

              cmds[0] = fill_rule;

              // encode solid fill and fp16v4 at cmds[1-3]
              spinel_styling_layer_fill_rgba_encoder(cmds + 1, rgba);

              cmds[4] = blend_mode;

#ifndef spinel_svg2SPINEL_DISABLE_OPACITY
              cmds[5] = SPN_STYLING_OPCODE_COLOR_ACC_TEST_OPACITY;
#endif
            }
            break;

            case SVG_LAYER_CMD_PLACE: {
              spinel(composition_place(composition,
                                       rasters + cmd->place.raster_index,
                                       &layer_id,
                                       NULL,  // &cmd->place.tx,
                                       1));
            }
            break;

            case SVG_LAYER_CMD_OPACITY: {
              opacity = cmd->opacity.opacity;
            }
            break;

            case SVG_LAYER_CMD_FILL_RULE: {
              fill_rule = (cmd->fill_rule.fill_rule == SVG_FILL_RULE_OP_NONZERO)
                            ? SPN_STYLING_OPCODE_COVER_NONZERO
                            : SPN_STYLING_OPCODE_COVER_EVENODD;
            }
            break;

            case SVG_LAYER_CMD_FILL_COLOR: {
              rgb = cmd->fill_color.fill_color;
            }
            break;

            case SVG_LAYER_CMD_FILL_OPACITY: {
              fill_opacity = cmd->fill_opacity.fill_opacity;
            }
            break;

            case SVG_LAYER_CMD_STROKE_COLOR: {  // FIXME(allanmac): IGNORED
            }
            break;

            case SVG_LAYER_CMD_STROKE_OPACITY: {  // FIXME(allanmac): IGNORED
              break;
            }
        }
    }

  svg_layer_iterator_dispose(iter);
}
//
//
//

void
spinel_svg_layers_decode_n(uint32_t const                svg_count,
                           struct svg const * const      svgs[],
                           spinel_raster_t const * const rasters[],
                           spinel_composition_t          composition,
                           spinel_styling_t              styling,
                           bool const                    is_srgb)
{
  //
  // get total layers
  //
  uint32_t total_layer_count = 0;

  for (uint32_t ii = 0; ii < svg_count; ii++)
    {
      total_layer_count += svg_layer_count(svgs[ii]);
    }

  //
  // create the top level styling group
  //
  spinel_group_id top_group_id;

  spinel(styling_group_alloc(styling, &top_group_id));

  //
  // enter
  //
  {
    spinel_styling_cmd_t * cmds_enter;

    spinel(styling_group_enter(styling, top_group_id, 1, &cmds_enter));

    cmds_enter[0] = SPN_STYLING_OPCODE_COLOR_ACC_ZERO;
  }

  //
  // leave
  //
  {
    spinel_styling_cmd_t * cmds_leave;

    spinel(styling_group_leave(styling, top_group_id, 4, &cmds_leave));

    // white for now
    float const background[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    // cmds[0-2]
    spinel_styling_background_over_encoder(cmds_leave, background);

    cmds_leave[3] = SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE_RGBA8;
  }

  // the top group has no parents
  spinel(styling_group_parents(styling, top_group_id, 0, NULL));

  // the range of the top group is [0,total_layer_count)
  spinel(styling_group_range_lo(styling, top_group_id, 0));
  spinel(styling_group_range_hi(styling, top_group_id, total_layer_count - 1));

  spinel_group_id const parents[] = { top_group_id };

  //
  // Each SVG receives its own group with no-op enter/leave commands.
  //
  spinel_layer_id layer_lo = 0;

  for (uint32_t ii = 0; ii < svg_count; ii++)
    {
      struct svg const * const svg = svgs[ii];

      // how many layers in the svg doc?
      uint32_t const layer_count = svg_layer_count(svg);

      if (layer_count == 0)
        continue;

      //
      // create the svg styling group
      //
      spinel_group_id group_id;

      spinel(styling_group_alloc(styling, &group_id));

      // declare the parents of the svg group
      spinel_group_id * svg_parents;

      spinel(styling_group_parents(styling, group_id, 1, &svg_parents));

      memcpy(svg_parents, parents, sizeof(*svg_parents) * 1);

      // the range of this group is [0,layer_count)
      spinel_layer_id const layer_hi = layer_lo + layer_count - 1;

      spinel(styling_group_range_lo(styling, group_id, layer_lo));
      spinel(styling_group_range_hi(styling, group_id, layer_hi));

      //
      // decode commands
      //
      spinel_svg_layers_decode_at(layer_lo,
                                  group_id,
                                  svg,
                                  rasters[ii],
                                  composition,
                                  styling,
                                  is_srgb);

      layer_lo += layer_count;
    }
}

//
//
//

void
spinel_svg_layers_decode(struct svg const * const      svg,
                         spinel_raster_t const * const rasters,
                         spinel_composition_t          composition,
                         spinel_styling_t              styling,
                         bool const                    is_srgb)
{
  spinel_svg_layers_decode_n(1, &svg, &rasters, composition, styling, is_srgb);
}

//
//
//
