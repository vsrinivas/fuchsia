// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/svg/svg_utils.h"

#include <math.h>

#include "tests/common/svg/svg_path_sink.h"

bool
svg_decode_path(const struct svg *         svg,
                uint32_t                   path_id,
                const affine_transform_t * transform,
                PathSink *                 target)
{
  bool        result = true;
  SvgPathSink svg_sink(target, transform);

  struct svg_path_iterator * iterator = svg_path_iterator_create(svg, path_id);
  union svg_path_cmd const * cmd;
  while (svg_path_iterator_next(iterator, &cmd))
    {
      switch (cmd->type)
        {
          case SVG_PATH_CMD_BEGIN:
            break;

          case SVG_PATH_CMD_END:
            break;

          case SVG_PATH_CMD_CIRCLE:
            svg_sink.circle(cmd->circle.cx, cmd->circle.cy, cmd->circle.r);
            break;

          case SVG_PATH_CMD_ELLIPSE:
            svg_sink.ellipse(cmd->ellipse.cx, cmd->ellipse.cy, cmd->ellipse.rx, cmd->ellipse.ry);
            break;

          case SVG_PATH_CMD_LINE:
            svg_sink.line(cmd->line.x1, cmd->line.y1, cmd->line.x2, cmd->line.y2);
            break;

          case SVG_PATH_CMD_POLYGON:
            svg_sink.polyStart(true);
            // This is followed by one or more SVG_PATH_CMD_POLY_POINT elements.
            break;

          case SVG_PATH_CMD_POLYLINE:
            svg_sink.polyStart(false);
            // This is followed by one or more SVG_PATH_CMD_POLY_POINT elements.
            break;

          case SVG_PATH_CMD_POLY_POINT:
            svg_sink.polyPoint(cmd->poly_point.x, cmd->poly_point.y);
            break;

          case SVG_PATH_CMD_POLY_END:
            svg_sink.polyEnd();
            break;

          case SVG_PATH_CMD_RECT:
            svg_sink.rect(cmd->rect.x, cmd->rect.y, cmd->rect.width, cmd->rect.height);
            break;

          case SVG_PATH_CMD_PATH_BEGIN:
            svg_sink.pathBegin();
            break;

          case SVG_PATH_CMD_PATH_END:
            if (!svg_sink.pathEnd())
              {
                result = false;
                goto Exit;
              }
            break;

          case SVG_PATH_CMD_MOVE_TO:
            svg_sink.moveTo(cmd->move_to.x, cmd->move_to.y);
            break;

          case SVG_PATH_CMD_MOVE_TO_REL:
            svg_sink.moveTo(cmd->move_to.x, cmd->move_to.y, true);
            break;

          case SVG_PATH_CMD_CLOSE_UPPER:
          case SVG_PATH_CMD_CLOSE:
            svg_sink.pathClose();
            break;

          case SVG_PATH_CMD_LINE_TO:
            svg_sink.lineTo(cmd->line_to.x, cmd->line_to.y);
            break;

          case SVG_PATH_CMD_LINE_TO_REL:
            svg_sink.lineTo(cmd->line_to.x, cmd->line_to.y, true);
            break;

          case SVG_PATH_CMD_HLINE_TO:
            svg_sink.hlineTo(cmd->hline_to.c);
            break;

          case SVG_PATH_CMD_HLINE_TO_REL:
            svg_sink.hlineTo(cmd->hline_to.c, true);
            break;

          case SVG_PATH_CMD_VLINE_TO:
            svg_sink.vlineTo(cmd->vline_to.c);
            break;

          case SVG_PATH_CMD_VLINE_TO_REL:
            svg_sink.vlineTo(cmd->vline_to.c, true);
            break;

          case SVG_PATH_CMD_CUBIC_TO:
          case SVG_PATH_CMD_CUBIC_TO_REL:
            svg_sink.cubicTo(cmd->cubic_to.x1,
                             cmd->cubic_to.y1,
                             cmd->cubic_to.x2,
                             cmd->cubic_to.y2,
                             cmd->cubic_to.x,
                             cmd->cubic_to.y,
                             cmd->type == SVG_PATH_CMD_CUBIC_TO_REL);
            break;

          case SVG_PATH_CMD_CUBIC_SMOOTH_TO:
          case SVG_PATH_CMD_CUBIC_SMOOTH_TO_REL:
            svg_sink.smoothCubicTo(cmd->cubic_smooth_to.x2,
                                   cmd->cubic_smooth_to.y2,
                                   cmd->cubic_smooth_to.x,
                                   cmd->cubic_smooth_to.y,
                                   cmd->type == SVG_PATH_CMD_CUBIC_SMOOTH_TO_REL);
            break;

          case SVG_PATH_CMD_QUAD_TO:
          case SVG_PATH_CMD_QUAD_TO_REL:
            svg_sink.quadTo(cmd->quad_to.x1,
                            cmd->quad_to.y1,
                            cmd->quad_to.x,
                            cmd->quad_to.y,
                            cmd->type == SVG_PATH_CMD_QUAD_TO_REL);
            break;

          case SVG_PATH_CMD_QUAD_SMOOTH_TO:
          case SVG_PATH_CMD_QUAD_SMOOTH_TO_REL:
            svg_sink.smoothQuadTo(cmd->quad_smooth_to.x,
                                  cmd->quad_smooth_to.y,
                                  cmd->type == SVG_PATH_CMD_QUAD_SMOOTH_TO_REL);
            break;

          case SVG_PATH_CMD_RAT_QUAD_TO:
          case SVG_PATH_CMD_RAT_QUAD_TO_REL:
            svg_sink.ratQuadTo(cmd->rat_quad_to.x1,
                               cmd->rat_quad_to.y1,
                               cmd->rat_quad_to.x,
                               cmd->rat_quad_to.y,
                               cmd->rat_quad_to.w1,
                               cmd->type == SVG_PATH_CMD_RAT_QUAD_TO_REL);
            break;

          case SVG_PATH_CMD_RAT_CUBIC_TO:
          case SVG_PATH_CMD_RAT_CUBIC_TO_REL:
            svg_sink.ratCubicTo(cmd->rat_cubic_to.x1,
                                cmd->rat_cubic_to.y1,
                                cmd->rat_cubic_to.x2,
                                cmd->rat_cubic_to.y2,
                                cmd->rat_cubic_to.x,
                                cmd->rat_cubic_to.y,
                                cmd->rat_cubic_to.w1,
                                cmd->rat_cubic_to.w2,
                                cmd->type == SVG_PATH_CMD_RAT_CUBIC_TO_REL);
            break;

          case SVG_PATH_CMD_ARC_TO:
          case SVG_PATH_CMD_ARC_TO_REL:
            svg_sink.arcTo(cmd->arc_to.x,
                           cmd->arc_to.y,
                           cmd->arc_to.rx,
                           cmd->arc_to.ry,
                           cmd->arc_to.x_axis_rotation * (M_PI / 180.),
                           cmd->arc_to.large_arc_flag != 0,
                           cmd->arc_to.sweep_flag != 0,
                           cmd->type == SVG_PATH_CMD_ARC_TO_REL);
            break;
        }
    }

Exit:
  svg_path_iterator_dispose(iterator);
  return result;
}

// Simple affine transform stack for SVG raster decoding.
class TransformStack {
 public:
  TransformStack(const affine_transform_t * transform)
  {
    stack_.push_back(transform ? *transform : affine_transform_identity);
    depth_ = 1;
  }

  const affine_transform_t *
  current() const
  {
    return &stack_.back();
  }

  void
  push(affine_transform_t transform)
  {
    // IMPORTANT: Svg transforms must be applied in reversed push order
    // which requires:  T + [A B C ...] => [(A * T) A B C ...]
    stack_.push_back(affine_transform_multiply(current(), &transform));
  }

  void
  pop()
  {
    if (stack_.size() > depth_)
      stack_.pop_back();
  }

 private:
  std::vector<affine_transform_t> stack_;
  uint32_t                        depth_;
};

bool
svg_decode_rasters(const struct svg *         svg,
                   const affine_transform_t * transform,
                   SvgDecodedRaster::Callback callback)
{
  bool           result = true;
  TransformStack transforms(transform);

  struct svg_raster_iterator * iterator = svg_raster_iterator_create(svg, UINT32_MAX);
  union svg_raster_cmd const * cmd;

  uint32_t raster_id = 0;
  while (svg_raster_iterator_next(iterator, &cmd))
    {
      switch (cmd->type)
        {
          case SVG_RASTER_CMD_BEGIN:
            // NOTE: Starting a new raster does *not* reset the transform stack.
            // instead, stack changes are carried from one raster to the next one
            // in the command list (making it impossible to decode an individual
            // raster properly without decoding all previous ones previously).
            break;

          case SVG_RASTER_CMD_END:
            raster_id++;
            break;

          case SVG_RASTER_CMD_FILL:
            result = callback((SvgDecodedRaster){
              .svg       = svg,
              .raster_id = raster_id,
              .path_id   = cmd->fill.path_index,
              .transform = *transforms.current(),
            });
            if (!result)
              goto Exit;
            break;

          case SVG_RASTER_CMD_STROKE:        // TODO(digit):
          case SVG_RASTER_CMD_MARKER:        // TODO(digit):
          case SVG_RASTER_CMD_STROKE_WIDTH:  // TODO(digit):
            break;

          case SVG_RASTER_CMD_TRANSFORM_PROJECT:
          case SVG_RASTER_CMD_TRANSFORM_MATRIX:
            transforms.push((const affine_transform_t){
              .sx  = cmd->matrix.sx,
              .shx = cmd->matrix.shx,
              .shy = cmd->matrix.shy,
              .sy  = cmd->matrix.sy,
              .tx  = cmd->matrix.tx,
              .ty  = cmd->matrix.ty,
            });
            break;

          case SVG_RASTER_CMD_TRANSFORM_TRANSLATE:
            transforms.push((const affine_transform_t){
              .sx = 1.,
              .sy = 1.,
              .tx = cmd->translate.tx,
              .ty = cmd->translate.ty,
            });
            break;

          case SVG_RASTER_CMD_TRANSFORM_SCALE:
            transforms.push((const affine_transform_t){
              .sx = cmd->scale.sx,
              .sy = cmd->scale.sy,
            });
            break;

            case SVG_RASTER_CMD_TRANSFORM_ROTATE: {
              transforms.push(affine_transform_make_rotation_xy(cmd->rotate.d * M_PI / 180.0,
                                                                cmd->rotate.cx,
                                                                cmd->rotate.cy));
              break;
            }

            case SVG_RASTER_CMD_TRANSFORM_SKEW_X: {
              transforms.push(affine_transform_make_skew_x(cmd->skew_x.d));
              break;
            }

            case SVG_RASTER_CMD_TRANSFORM_SKEW_Y: {
              transforms.push(affine_transform_make_skew_y(cmd->skew_y.d));
              break;
            }

          case SVG_RASTER_CMD_TRANSFORM_DROP:
            transforms.pop();
            break;
        }
    }

Exit:
  svg_raster_iterator_dispose(iterator);
  return result;
}

bool
svg_decode_layers(const svg * svg, SvgDecodedLayer::Callback callback)
{
  bool                 result = true;
  svg_layer_iterator * iter   = svg_layer_iterator_create(svg, UINT32_MAX);

  const union svg_layer_cmd * cmd;
  SvgDecodedLayer             layer = {
    .svg           = svg,
    .layer_id      = 0,
    .fill_color    = 0,
    .fill_opacity  = 1.,
    .opacity       = 1.,
    .fill_even_odd = false,
    .prints        = {},
  };

  while (svg_layer_iterator_next(iter, &cmd))
    {
      switch (cmd->type)
        {
          case SVG_LAYER_CMD_BEGIN:
            break;

          case SVG_LAYER_CMD_END:
            result = callback(layer);
            if (!result)
              goto Exit;

            layer.layer_id++;
            layer.prints.clear();
            break;

            case SVG_LAYER_CMD_PLACE: {
              layer.prints.push_back(SvgDecodedLayer::Print{
                .raster_id = cmd->place.raster_index,
                .tx        = cmd->place.tx,
                .ty        = cmd->place.ty,
              });
              break;
            }

          case SVG_LAYER_CMD_OPACITY:
            layer.opacity = cmd->opacity.opacity;
            break;

          case SVG_LAYER_CMD_FILL_RULE:
            layer.fill_even_odd = (cmd->fill_rule.fill_rule == SVG_FILL_RULE_OP_EVENODD);
            break;

          case SVG_LAYER_CMD_FILL_COLOR:
            layer.fill_color = cmd->fill_color.fill_color;
            break;

          case SVG_LAYER_CMD_FILL_OPACITY:
            layer.fill_opacity = cmd->fill_opacity.fill_opacity;
            break;

          case SVG_LAYER_CMD_STROKE_COLOR:
          case SVG_LAYER_CMD_STROKE_OPACITY:
            // IGNORED FOR NOW.
            break;
        }
    }

Exit:
  svg_layer_iterator_dispose(iter);
  return result;
}
