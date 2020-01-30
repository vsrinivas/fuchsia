// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svg2spinel.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#include "spinel/ext/color/color.h"
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
spn_svg_paths_release(const struct svg * const svg, spn_context_t context, spn_path_t * const paths)
{
  uint32_t const n = svg_path_count(svg);

  spn(path_release(context, paths, n));

  free(paths);
}

void
spn_svg_rasters_release(const struct svg * const svg,
                        spn_context_t            context,
                        spn_raster_t * const     rasters)

{
  uint32_t const n = svg_raster_count(svg);

  spn(raster_release(context, rasters, n));

  free(rasters);
}

//
//
//

static void
spn_svg_poly_read(struct svg_path_iterator * iter, spn_path_builder_t pb, bool const close)
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
          spn(path_builder_line_to(pb, cmd->poly_point.x, cmd->poly_point.y));
        }
      else
        {
          spn(path_builder_move_to(pb, x0 = cmd->poly_point.x, y0 = cmd->poly_point.y));
          lineto = true;
        }
    }

  if (close && lineto)
    {
      spn(path_builder_line_to(pb, x0, y0));
    }
}

//
//
//

static void
spn_svg_implicit_close_filled_path(spn_path_builder_t pb, float x0, float y0, float x, float y)
{
  if ((x != x0) || (y != y0))
    spn(path_builder_line_to(pb, x0, y0));
}

//
//
//

static void
spn_svg_arc_decode(bool const                               is_relative,
                   struct svg_path_cmd_arc_to const * const cmd_arc_to,
                   float * const                            x,
                   float * const                            y,
                   spn_path_builder_t                       pb)
{
  float x1 = cmd_arc_to->x;
  float y1 = cmd_arc_to->y;

  fprintf(stderr, "Error: arc not implemented - requires rationals\n");

  *x = x1;
  *y = y1;
}

//
//
//

spn_path_t *
spn_svg_paths_decode(struct svg const * const svg, spn_path_builder_t pb)
{
  spn_path_t * const paths = malloc(sizeof(*paths) * svg_path_count(svg));

  union svg_path_cmd const * cmd;

  float x0, y0, x, y;

  struct svg_path_iterator * iter = svg_path_iterator_create(svg, UINT32_MAX);

  while (svg_path_iterator_next(iter, &cmd))
    {
      switch (cmd->type)
        {
          case SVG_PATH_CMD_BEGIN:
            spn(path_builder_begin(pb));
            break;

          case SVG_PATH_CMD_END:
            spn(path_builder_end(pb, paths + cmd->end.path_index));
            break;

          case SVG_PATH_CMD_CIRCLE:
            spn(path_builder_ellipse(pb,
                                     cmd->circle.cx,
                                     cmd->circle.cy,
                                     cmd->circle.r,
                                     cmd->circle.r));
            break;

          case SVG_PATH_CMD_ELLIPSE:
            spn(path_builder_ellipse(pb,
                                     cmd->ellipse.cx,
                                     cmd->ellipse.cy,
                                     cmd->ellipse.rx,
                                     cmd->ellipse.ry));
            break;

          case SVG_PATH_CMD_LINE:
            spn(path_builder_move_to(pb, cmd->line.x1, cmd->line.y1));
            spn(path_builder_line_to(pb, cmd->line.x2, cmd->line.y2));
            break;

          case SVG_PATH_CMD_POLYGON:
            spn_svg_poly_read(iter, pb, true);
            break;

          case SVG_PATH_CMD_POLYLINE:
            spn_svg_poly_read(iter, pb, false);
            break;

          case SVG_PATH_CMD_RECT:
            spn(path_builder_move_to(pb, cmd->rect.x, cmd->rect.y));
            spn(path_builder_line_to(pb, cmd->rect.x + cmd->rect.width, cmd->rect.y));
            spn(path_builder_line_to(pb,
                                     cmd->rect.x + cmd->rect.width,
                                     cmd->rect.y + cmd->rect.height));
            spn(path_builder_line_to(pb, cmd->rect.x, cmd->rect.y + cmd->rect.height));
            spn(path_builder_line_to(pb, cmd->rect.x, cmd->rect.y));
            break;

          case SVG_PATH_CMD_PATH_BEGIN:
            x = x0 = 0.0f;
            y = y0 = 0.0f;
            break;

          case SVG_PATH_CMD_PATH_END:
            spn_svg_implicit_close_filled_path(pb, x0, y0, x, y);
            break;

          case SVG_PATH_CMD_MOVE_TO:
            spn_svg_implicit_close_filled_path(pb, x0, y0, x, y);
            spn_path_builder_move_to(pb, x0 = x = cmd->move_to.x, y0 = y = cmd->move_to.y);
            break;

          case SVG_PATH_CMD_MOVE_TO_REL:
            spn_svg_implicit_close_filled_path(pb, x0, y0, x, y);
            spn(path_builder_move_to(pb, x0 = (x += cmd->move_to.x), y0 = (y += cmd->move_to.y)));
            break;

          case SVG_PATH_CMD_CLOSE_UPPER:
          case SVG_PATH_CMD_CLOSE:
            spn_svg_implicit_close_filled_path(pb, x0, y0, x, y);
            spn(path_builder_move_to(pb, x = x0, y = y0));  // reset to initial point
            break;

          case SVG_PATH_CMD_LINE_TO:
            spn(path_builder_line_to(pb, x = cmd->line_to.x, y = cmd->line_to.y));
            break;

          case SVG_PATH_CMD_LINE_TO_REL:
            spn(path_builder_line_to(pb, x += cmd->line_to.x, y += cmd->line_to.y));
            break;

          case SVG_PATH_CMD_HLINE_TO:
            spn(path_builder_line_to(pb, x = cmd->hline_to.c, y));
            break;

          case SVG_PATH_CMD_HLINE_TO_REL:
            spn(path_builder_line_to(pb, x += cmd->hline_to.c, y));
            break;

          case SVG_PATH_CMD_VLINE_TO:
            spn(path_builder_line_to(pb, x, y = cmd->vline_to.c));
            break;

          case SVG_PATH_CMD_VLINE_TO_REL:
            spn(path_builder_line_to(pb, x, y += cmd->vline_to.c));
            break;

          case SVG_PATH_CMD_CUBIC_TO:
            spn(path_builder_cubic_to(pb,
                                      cmd->cubic_to.x1,
                                      cmd->cubic_to.y1,
                                      cmd->cubic_to.x2,
                                      cmd->cubic_to.y2,
                                      x = cmd->cubic_to.x,
                                      y = cmd->cubic_to.y));
            break;

          case SVG_PATH_CMD_CUBIC_TO_REL:
            spn(path_builder_cubic_to(pb,
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
            spn(path_builder_cubic_smooth_to(pb,
                                             cmd->cubic_smooth_to.x2,
                                             cmd->cubic_smooth_to.y2,
                                             x = cmd->cubic_smooth_to.x,
                                             y = cmd->cubic_smooth_to.y));
            break;

          case SVG_PATH_CMD_CUBIC_SMOOTH_TO_REL:
            spn(path_builder_cubic_smooth_to(pb,
                                             x + cmd->cubic_smooth_to.x2,
                                             y + cmd->cubic_smooth_to.y2,
                                             x + cmd->cubic_smooth_to.x,
                                             y + cmd->cubic_smooth_to.y));
            x += cmd->cubic_smooth_to.x;
            y += cmd->cubic_smooth_to.y;
            break;

          case SVG_PATH_CMD_QUAD_TO:
            spn(path_builder_quad_to(pb,
                                     cmd->quad_to.x1,
                                     cmd->quad_to.y1,
                                     x = cmd->quad_to.x,
                                     y = cmd->quad_to.y));
            break;

          case SVG_PATH_CMD_QUAD_TO_REL:
            spn(path_builder_quad_to(pb,
                                     x + cmd->quad_to.x1,
                                     y + cmd->quad_to.y1,
                                     x + cmd->quad_to.x,
                                     y + cmd->quad_to.y));
            x += cmd->quad_to.x;
            y += cmd->quad_to.y;
            break;

          case SVG_PATH_CMD_QUAD_SMOOTH_TO:
            spn(path_builder_quad_smooth_to(pb,
                                            x = cmd->quad_smooth_to.x,
                                            y = cmd->quad_smooth_to.y));
            break;

          case SVG_PATH_CMD_QUAD_SMOOTH_TO_REL:
            spn(path_builder_quad_smooth_to(pb,
                                            x + cmd->quad_smooth_to.x,
                                            y + cmd->quad_smooth_to.y));
            x += cmd->quad_smooth_to.x;
            y += cmd->quad_smooth_to.y;
            break;

          case SVG_PATH_CMD_RAT_CUBIC_TO:
            spn(path_builder_rat_cubic_to(pb,
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
            spn(path_builder_rat_cubic_to(pb,
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
            spn(path_builder_rat_quad_to(pb,
                                         cmd->rat_quad_to.x1,
                                         cmd->rat_quad_to.y1,
                                         x = cmd->rat_quad_to.x,
                                         y = cmd->rat_quad_to.y,
                                         cmd->rat_quad_to.w1));
            break;

          case SVG_PATH_CMD_RAT_QUAD_TO_REL:
            spn(path_builder_rat_quad_to(pb,
                                         x + cmd->rat_quad_to.x1,
                                         y + cmd->rat_quad_to.y1,
                                         x + cmd->rat_quad_to.x,
                                         y + cmd->rat_quad_to.y,
                                         cmd->rat_quad_to.w1));
            x += cmd->rat_quad_to.x;
            y += cmd->rat_quad_to.y;
            break;

          case SVG_PATH_CMD_ARC_TO:
            spn_svg_arc_decode(false, &cmd->arc_to, &x, &y, pb);
            break;

          case SVG_PATH_CMD_ARC_TO_REL:
            spn_svg_arc_decode(true, &cmd->arc_to, &x, &y, pb);
            break;

          default:
            fprintf(stderr, "Error: unhandled path type - %u\n", cmd->type);
            exit(-1);
        }
    }

  svg_path_iterator_dispose(iter);

  return paths;
}

//
//
//

spn_raster_t *
spn_svg_rasters_decode(struct svg const * const       svg,
                       spn_raster_builder_t           rb,
                       spn_path_t const * const       paths,
                       struct transform_stack * const ts)
{
  static struct spn_clip const raster_clips[] = { { 0.0f, 0.0f, FLT_MAX, FLT_MAX } };

  spn_raster_t * const rasters    = malloc(sizeof(*rasters) * svg_raster_count(svg));
  uint32_t const       ts_restore = transform_stack_save(ts);

  union svg_raster_cmd const * cmd;

  struct svg_raster_iterator * iter = svg_raster_iterator_create(svg, UINT32_MAX);

  while (svg_raster_iterator_next(iter, &cmd))
    {
      switch (cmd->type)
        {
          case SVG_RASTER_CMD_BEGIN:
            spn(raster_builder_begin(rb));
            break;

          case SVG_RASTER_CMD_END:
            spn(raster_builder_end(rb, rasters + cmd->end.raster_index));
            break;

          case SVG_RASTER_CMD_FILL:
            spn(raster_builder_add(rb,
                                   paths + cmd->fill.path_index,
                                   NULL,  // transform_stack_top_weakref(ts),
                                   (spn_transform_t *)transform_stack_top_transform(ts),
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
            transform_stack_push_matrix(ts,
                                        cmd->project.sx,
                                        cmd->project.shx,
                                        cmd->project.tx,
                                        cmd->project.shy,
                                        cmd->project.sy,
                                        cmd->project.ty,
                                        cmd->project.w0,
                                        cmd->project.w1,
                                        1);
            transform_stack_concat(ts);
            break;

          case SVG_RASTER_CMD_TRANSFORM_MATRIX:
            transform_stack_push_affine(ts,
                                        cmd->matrix.sx,
                                        cmd->matrix.shx,
                                        cmd->matrix.tx,
                                        cmd->matrix.shy,
                                        cmd->matrix.sy,
                                        cmd->matrix.ty);
            transform_stack_concat(ts);
            break;

          case SVG_RASTER_CMD_TRANSFORM_TRANSLATE:
            transform_stack_push_translate(ts, cmd->translate.tx, cmd->translate.ty);
            transform_stack_concat(ts);
            break;

          case SVG_RASTER_CMD_TRANSFORM_SCALE:
            transform_stack_push_scale(ts, cmd->scale.sx, cmd->scale.sy);
            transform_stack_concat(ts);
            break;

          case SVG_RASTER_CMD_TRANSFORM_ROTATE:
            transform_stack_push_rotate_xy(ts,
                                           cmd->rotate.d * (float)(M_PI / 180.0),
                                           cmd->rotate.cx,
                                           cmd->rotate.cy);
            transform_stack_concat(ts);
            break;

          case SVG_RASTER_CMD_TRANSFORM_SKEW_X:
            transform_stack_push_skew_x(ts, cmd->skew_x.d * (float)(M_PI / 180.0));
            transform_stack_concat(ts);
            break;

          case SVG_RASTER_CMD_TRANSFORM_SKEW_Y:
            transform_stack_push_skew_y(ts, cmd->skew_y.d * (float)(M_PI / 180.0));
            transform_stack_concat(ts);
            break;

          case SVG_RASTER_CMD_TRANSFORM_DROP:
            transform_stack_drop(ts);
            break;
        }
    }

  // restore stack depth
  transform_stack_restore(ts, ts_restore);

  svg_raster_iterator_dispose(iter);

  return rasters;
}

//
//
//

void
spn_styling_background_over_encoder(spn_styling_cmd_t * const cmds, float const colors[]);

//
//
//

void
spn_svg_layers_decode(struct svg const * const   svg,
                      spn_raster_t const * const rasters,
                      spn_composition_t          composition,
                      spn_styling_t              styling,
                      bool const                 is_srgb)
{
  //
  // create the top level styling group
  //
  spn_group_id group_id;

  spn(styling_group_alloc(styling, &group_id));

  //
  // enter
  //
  {
    spn_styling_cmd_t * cmds_enter;

    spn(styling_group_enter(styling, group_id, 1, &cmds_enter));

    cmds_enter[0] = SPN_STYLING_OPCODE_COLOR_ACC_ZERO;
  }

  //
  // leave
  //
  {
    spn_styling_cmd_t * cmds_leave;

    spn(styling_group_leave(styling, group_id, 4, &cmds_leave));

    // white for now
    float const background[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    // cmds[0-2]
    spn_styling_background_over_encoder(cmds_leave, background);

    cmds_leave[3] = SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE;
  }

  // this is the root group
  spn(styling_group_parents(styling, group_id, 0, NULL));

  // how many layers in the svg doc?
  uint32_t const layer_count = svg_layer_count(svg);

  // the range of the root group is maximal [0,layer_count)
  spn(styling_group_range_lo(styling, group_id, 0));
  spn(styling_group_range_hi(styling, group_id, layer_count - 1));

  //
  //
  //
  union svg_layer_cmd const * cmd;

  spn_layer_id layer_id;

  spn_styling_cmd_t fill_rule  = SPN_STYLING_OPCODE_COVER_NONZERO;
  spn_styling_cmd_t blend_mode = SPN_STYLING_OPCODE_BLEND_OVER;

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
              layer_id = layer_count - 1 - cmd->begin.layer_index;
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

              spn_styling_cmd_t * cmds;

#define SPN_SVG2SPINEL_DISABLE_OPACITY
#ifndef SPN_SVG2SPINEL_DISABLE_OPACITY
              spn(styling_group_layer(styling, group_id, layer_id, 6, &cmds));
#else
              spn(styling_group_layer(styling, group_id, layer_id, 5, &cmds));
#endif

              cmds[0] = fill_rule;

              // encode solid fill and fp16v4 at cmds[1-3]
              spn_styling_layer_fill_rgba_encoder(cmds + 1, rgba);

              cmds[4] = blend_mode;

#ifndef SPN_SVG2SPINEL_DISABLE_OPACITY
              cmds[5] = SPN_STYLING_OPCODE_COLOR_ACC_TEST_OPACITY;
#endif
            }
            break;

            case SVG_LAYER_CMD_PLACE: {
              spn(composition_place(composition,
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
