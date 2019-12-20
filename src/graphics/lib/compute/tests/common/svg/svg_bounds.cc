// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svg_bounds.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#include "tests/common/affine_transform.h"

static void
bounds_update_point(double * const bounds, double x, double y, const affine_transform_stack_t * ts)
{
  affine_transform_apply(affine_transform_stack_top(ts), &x, &y);
  if (x < bounds[0])
    bounds[0] = x;
  if (y < bounds[1])
    bounds[1] = y;
  if (x > bounds[2])
    bounds[2] = x;
  if (y > bounds[3])
    bounds[3] = y;
}

static void
bounds_update_line(double * const                   bounds,
                   double const                     x1,
                   double const                     y1,
                   double const                     x2,
                   double const                     y2,
                   affine_transform_stack_t const * ts)
{
  bounds_update_point(bounds, x1, y1, ts);
  bounds_update_point(bounds, x2, y2, ts);
}

static void
bounds_update_rect(double * const                   bounds,
                   double const                     x1,
                   double const                     y1,
                   double const                     x2,
                   double const                     y2,
                   affine_transform_stack_t const * ts)
{
  bounds_update_point(bounds, x1, y1, ts);
  bounds_update_point(bounds, x2, y1, ts);
  bounds_update_point(bounds, x1, y2, ts);
  bounds_update_point(bounds, x2, y2, ts);
}

static void
bounds_update_arc(double * const                   bounds,
                  union svg_path_cmd const *       cmd,
                  double * const                   x,
                  double * const                   y,
                  affine_transform_stack_t const * ts,
                  bool                             relative_coords)
{
  // TODO(digit): Implement this
  if (relative_coords)
    {
      *x += cmd->arc_to.x;
      *y += cmd->arc_to.y;
    }
  else
    {
      *x = cmd->arc_to.x;
      *y = cmd->arc_to.y;
    }
  bounds_update_point(bounds, *x, *y, ts);
}

static void
bounds_update_path(double * const                   bounds,
                   struct svg const * const         sd,
                   uint32_t                         path_id,
                   affine_transform_stack_t const * ts)
{
  double x0 = 0., y0 = 0., x = 0., y = 0.;

  struct svg_path_iterator * iterator = svg_path_iterator_create(sd, path_id);

  union svg_path_cmd const * cmd;
  while (svg_path_iterator_next(iterator, &cmd))
    {
      switch (cmd->type)
        {
          case SVG_PATH_CMD_BEGIN:
          case SVG_PATH_CMD_END:
            break;

          case SVG_PATH_CMD_CIRCLE:
            bounds_update_rect(bounds,
                               cmd->circle.cx - cmd->circle.r,
                               cmd->circle.cy - cmd->circle.r,
                               cmd->circle.cx + cmd->circle.r,
                               cmd->circle.cy + cmd->circle.r,
                               ts);
            break;

          case SVG_PATH_CMD_ELLIPSE:
            bounds_update_rect(bounds,
                               cmd->ellipse.cx - cmd->ellipse.rx,
                               cmd->ellipse.cy - cmd->ellipse.ry,
                               cmd->ellipse.cx + cmd->ellipse.rx,
                               cmd->ellipse.cy + cmd->ellipse.ry,
                               ts);
            break;

          case SVG_PATH_CMD_LINE:
            bounds_update_line(bounds, cmd->line.x1, cmd->line.y1, cmd->line.x2, cmd->line.y2, ts);
            break;

          case SVG_PATH_CMD_POLYGON:
          case SVG_PATH_CMD_POLYLINE:
            // This is followed by one or more SVG_PATH_CMD_POLY_POINT elements.
            break;

          case SVG_PATH_CMD_POLY_POINT:
            bounds_update_point(bounds, cmd->poly_point.x, cmd->poly_point.y, ts);
            break;

          case SVG_PATH_CMD_POLY_END:
            // Just a marker for the end.
            break;

          case SVG_PATH_CMD_RECT:
            bounds_update_rect(bounds,
                               cmd->rect.x,
                               cmd->rect.y,
                               cmd->rect.x + cmd->rect.width,
                               cmd->rect.y + cmd->rect.height,
                               ts);
            break;

          case SVG_PATH_CMD_PATH_BEGIN:
            x = x0 = 0.0f;
            y = y0 = 0.0f;
            break;

          case SVG_PATH_CMD_PATH_END:
            break;

          case SVG_PATH_CMD_MOVE_TO:
            x0 = x = cmd->move_to.x;
            y0 = y = cmd->move_to.y;
            bounds_update_point(bounds, x, y, ts);
            break;

          case SVG_PATH_CMD_MOVE_TO_REL:
            x0 = (x += cmd->move_to.x);
            y0 = (y += cmd->move_to.y);
            bounds_update_point(bounds, x, y, ts);
            break;

          case SVG_PATH_CMD_CLOSE_UPPER:
          case SVG_PATH_CMD_CLOSE:
            x = x0;
            y = y0;  // reset to initial point
            break;

          case SVG_PATH_CMD_LINE_TO:
            bounds_update_point(bounds, x = cmd->line_to.x, y = cmd->line_to.y, ts);
            break;

          case SVG_PATH_CMD_LINE_TO_REL:
            bounds_update_point(bounds, x += cmd->line_to.x, y += cmd->line_to.y, ts);
            break;

          case SVG_PATH_CMD_HLINE_TO:
            bounds_update_point(bounds, x = cmd->hline_to.c, y, ts);
            break;

          case SVG_PATH_CMD_HLINE_TO_REL:
            bounds_update_point(bounds, x += cmd->hline_to.c, y, ts);
            break;

          case SVG_PATH_CMD_VLINE_TO:
            bounds_update_point(bounds, x, y = cmd->vline_to.c, ts);
            break;

          case SVG_PATH_CMD_VLINE_TO_REL:
            bounds_update_point(bounds, x, y += cmd->vline_to.c, ts);
            break;

          case SVG_PATH_CMD_CUBIC_TO:
            bounds_update_line(bounds,
                               cmd->cubic_to.x1,
                               cmd->cubic_to.y1,
                               cmd->cubic_to.x2,
                               cmd->cubic_to.y2,
                               ts);
            bounds_update_point(bounds, x = cmd->cubic_to.x, y = cmd->cubic_to.y, ts);
            break;

          case SVG_PATH_CMD_CUBIC_TO_REL:
            bounds_update_line(bounds,
                               x + cmd->cubic_to.x1,
                               y + cmd->cubic_to.y1,
                               x + cmd->cubic_to.x2,
                               y + cmd->cubic_to.y2,
                               ts);
            bounds_update_point(bounds, x += cmd->cubic_to.x, y += cmd->cubic_to.y, ts);
            break;

          case SVG_PATH_CMD_CUBIC_SMOOTH_TO:
            bounds_update_line(bounds,
                               cmd->cubic_smooth_to.x2,
                               cmd->cubic_smooth_to.y2,
                               x = cmd->cubic_smooth_to.x,
                               y = cmd->cubic_smooth_to.y,
                               ts);
            break;

          case SVG_PATH_CMD_CUBIC_SMOOTH_TO_REL:
            bounds_update_point(bounds,
                                x + cmd->cubic_smooth_to.x2,
                                y + cmd->cubic_smooth_to.y2,
                                ts);
            bounds_update_point(bounds,
                                x += cmd->cubic_smooth_to.x,
                                y += cmd->cubic_smooth_to.y,
                                ts);
            break;

          case SVG_PATH_CMD_QUAD_TO:
            bounds_update_line(bounds,
                               cmd->quad_to.x1,
                               cmd->quad_to.y1,
                               x = cmd->quad_to.x,
                               y = cmd->quad_to.y,
                               ts);
            break;

          case SVG_PATH_CMD_QUAD_TO_REL:
            bounds_update_point(bounds, x + cmd->quad_to.x1, y + cmd->quad_to.y1, ts);
            bounds_update_point(bounds, x += cmd->quad_to.x, y += cmd->quad_to.y, ts);
            break;

          case SVG_PATH_CMD_QUAD_SMOOTH_TO:
            bounds_update_point(bounds, x = cmd->quad_smooth_to.x, y = cmd->quad_smooth_to.y, ts);
            break;

          case SVG_PATH_CMD_QUAD_SMOOTH_TO_REL:
            bounds_update_point(bounds, x += cmd->quad_smooth_to.x, y += cmd->quad_smooth_to.y, ts);
            break;

          case SVG_PATH_CMD_ARC_TO:
            bounds_update_arc(bounds, cmd, &x, &y, ts, false);
            break;

          case SVG_PATH_CMD_ARC_TO_REL:
            bounds_update_arc(bounds, cmd, &x, &y, ts, true);
            break;
        }
    }
  svg_path_iterator_dispose(iterator);
}

void
svg_estimate_bounds(struct svg const * const   sd,
                    const affine_transform_t * transform,
                    double * const             xmin,
                    double * const             ymin,
                    double * const             xmax,
                    double * const             ymax)
{
  affine_transform_stack_t * ts = affine_transform_stack_create();
  if (transform)
    affine_transform_stack_push(ts, *transform);

  struct svg_raster_iterator * iterator = svg_raster_iterator_create(sd, UINT32_MAX);
  union svg_raster_cmd const * cmd;

  double bounds[4] = { DBL_MAX, DBL_MAX, -DBL_MAX, -DBL_MAX };

  while (svg_raster_iterator_next(iterator, &cmd))
    {
      switch (cmd->type)
        {
          case SVG_RASTER_CMD_BEGIN:
          case SVG_RASTER_CMD_END:
            break;

          case SVG_RASTER_CMD_FILL:
            bounds_update_path(bounds, sd, cmd->fill.path_index, ts);
            break;

          case SVG_RASTER_CMD_STROKE:        // TODO(digit):
          case SVG_RASTER_CMD_MARKER:        // TODO(digit):
          case SVG_RASTER_CMD_STROKE_WIDTH:  // TODO(digit):
            break;

          case SVG_RASTER_CMD_TRANSFORM_MATRIX:
            affine_transform_stack_push(ts,
                                        (const affine_transform_t){
                                          .sx  = cmd->matrix.sx,
                                          .shx = cmd->matrix.shx,
                                          .shy = cmd->matrix.shy,
                                          .sy  = cmd->matrix.sy,
                                          .tx  = cmd->matrix.tx,
                                          .ty  = cmd->matrix.ty,
                                        });
            break;

          case SVG_RASTER_CMD_TRANSFORM_TRANSLATE:
            affine_transform_stack_push(ts,
                                        (const affine_transform_t){
                                          .tx = cmd->translate.tx,
                                          .ty = cmd->translate.ty,
                                        });
            break;

          case SVG_RASTER_CMD_TRANSFORM_SCALE:
            affine_transform_stack_push(ts,
                                        (const affine_transform_t){
                                          .sx = cmd->scale.sx,
                                          .sy = cmd->scale.sy,
                                        });
            break;

            case SVG_RASTER_CMD_TRANSFORM_ROTATE: {
              affine_transform_stack_push(
                ts,
                affine_transform_make_rotation_xy(cmd->rotate.d * (float)(M_PI / 180.0),
                                                  cmd->rotate.cx,
                                                  cmd->rotate.cy));
              break;
            }

            case SVG_RASTER_CMD_TRANSFORM_SKEW_X: {
              affine_transform_stack_push(ts, affine_transform_make_skew_x(cmd->skew_x.d));
              break;
            }

            case SVG_RASTER_CMD_TRANSFORM_SKEW_Y: {
              affine_transform_stack_push(ts, affine_transform_make_skew_y(cmd->skew_y.d));
              break;
            }

          case SVG_RASTER_CMD_TRANSFORM_DROP:
            affine_transform_stack_pop(ts);
            break;
        }
    }

  svg_raster_iterator_dispose(iterator);

  affine_transform_stack_destroy(ts);

  *xmin = bounds[0];
  *ymin = bounds[1];
  *xmax = bounds[2];
  *ymax = bounds[3];
}
