// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svg_print.h"

#include <sstream>

#include "svg/svg.h"
#include "tests/common/list_ostream.h"

// Helper struct to handle the ... to va_list conversion.
// Usage is:
//    1) Create instance, passing the print_func and print_opaque arguments.
//    2) Call print(...) method repeatedly.
//    3) Alternatively, print C++ iostream with << as in:
//            printer << "Some value: " << value;
//
struct Printer
{
  Printer(svg_printf_func_t * print_func, void * print_opaque)
      : print_(print_func), opaque_(print_opaque)
  {
  }

  void
  print(const char * fmt, ...)
  {
    va_list args;
    va_start(args, fmt);
    print_(opaque_, fmt, args);
    va_end(args);
  }

  void
  print(const std::string & str)
  {
    print("%.*s", str.size(), str.c_str());
  }

 private:
  svg_printf_func_t * print_;
  void *              opaque_;
};

static std::string
colorToString(svg_color_t color)
{
  char     temp[32];
  unsigned r = (color >> 16) & 255;
  unsigned g = (color >> 8) & 255;
  unsigned b = (color & 255);
  snprintf(temp, sizeof(temp), "r:%u,g:%u,b:%u", r, g, b);
  return std::string(temp);
}

static const char *
fillRuleToString(svg_fill_rule_op rule)
{
  switch (rule)
    {
      case SVG_FILL_RULE_OP_EVENODD:
        return "EvenOdd";
      case SVG_FILL_RULE_OP_NONZERO:
        return "NonZero";
    }
}

void
svg_print(const struct svg * svg, svg_printf_func_t * print_func, void * print_opaque)
{
  Printer p(print_func, print_opaque);

  uint32_t path_count   = svg_path_count(svg);
  uint32_t raster_count = svg_raster_count(svg);
  uint32_t layer_count  = svg_layer_count(svg);

  p.print("SVG Document (paths=%u,rasters=%u,layers=%u) {\n",
          path_count,
          raster_count,
          layer_count);

  //
  // Paths
  //
  for (uint32_t path_index = 0; path_index < path_count; ++path_index)
    {
      // Separate command items with a list_ostream
      std::stringstream ss;
      list_ostream      ls(ss);
      ls.set_comma(", ");

      // A list ostream used to collect polygon and polyline point coordinates.
      std::stringstream poly_ss;
      list_ostream      poly_ls(poly_ss);
      const char *      in_polyline = nullptr;

      std::stringstream path_ss;
      list_ostream      path_ls(path_ss);
      bool              in_path = false;

      p.print("  path[%u]: ", path_index);
      svg_path_iterator *        iterator = svg_path_iterator_create(svg, path_index);
      const union svg_path_cmd * cmd;
      while (svg_path_iterator_next(iterator, &cmd))
        {
          // Special handling for polygon/polylines.
          if (in_polyline != nullptr && cmd->type != SVG_PATH_CMD_POLY_POINT)
            {
              ls << in_polyline << "(" << poly_ss.str() << ")" << ls.comma;
              in_polyline = nullptr;
              poly_ss.str("");
            }

          if (in_path && cmd->type == SVG_PATH_CMD_END)
            {
              ls << "Path(" << path_ss.str() << ")" << ls.comma;
              in_path = false;
              path_ss.str("");
            }

          // clang-format off
          switch (cmd->type) {
          case SVG_PATH_CMD_BEGIN:
          case SVG_PATH_CMD_END:
            // Don't print begin and end commands.
            break;

          case SVG_PATH_CMD_CIRCLE:
            ls << "Circle"
              << "(cx:" << cmd->circle.cx
              << ",cy:" << cmd->circle.cy
              << ",r:"  << cmd->circle.r
              << ")" << ls.comma;
            break;

          case SVG_PATH_CMD_ELLIPSE:
            ls << "Ellipse"
              << "(cx:" << cmd->ellipse.cx
              << ",cy:" << cmd->ellipse.cy
              << ",rx:" << cmd->ellipse.rx
              << ",ry:" << cmd->ellipse.ry
              << ")" << ls.comma;
            break;

          case SVG_PATH_CMD_LINE:
            ls << "Line"
              << "(x1:" << cmd->line.x1
              << ",y1:" << cmd->line.y1
              << ",x2:" << cmd->line.x2
              << ",y2:" << cmd->line.y2
              << ")" << ls.comma;
            break;

          case SVG_PATH_CMD_POLYGON:
            in_polyline = "Polygon";
            break;

          case SVG_PATH_CMD_POLYLINE:
            in_polyline = "Polyline";
            break;

          case SVG_PATH_CMD_RECT:
            ls << "Rect"
              << "(x:" << cmd->rect.x
              << ",y:" << cmd->rect.y
              << ",w:" << cmd->rect.width
              << ",h:" << cmd->rect.height;
            if (cmd->rect.rx || cmd->rect.ry)
              ls << ",rx:" << cmd->rect.rx << ",ry:" << cmd->rect.ry;
            ls << ")" << ls.comma;
            break;

          case SVG_PATH_CMD_POLY_POINT:
            // NOTE: This accumulate coordinates in poly_ls instead of ls.
            poly_ls
                << "(" << cmd->poly_point.x
                << "," << cmd->poly_point.y
                << ")"
                << poly_ls.comma;
            break;

          case SVG_PATH_CMD_POLY_END:
            // Already handled before the switch() above.
            break;

          case SVG_PATH_CMD_PATH_BEGIN:
            in_path = true;
            break;

          case SVG_PATH_CMD_PATH_END:
            // Already handled before the switch() above.
            break;

          case SVG_PATH_CMD_MOVE_TO:
            path_ls << "MoveTo"
                    << "(x:" << cmd->move_to.x
                    << ",y:" << cmd->move_to.y
                    << ")" << path_ls.comma;
            break;

          case SVG_PATH_CMD_MOVE_TO_REL:
            path_ls << "MoveToRel"
                    << "(dx:" << cmd->move_to.x
                    << ",dy:" << cmd->move_to.y
                    << ")" << path_ls.comma;
            break;

          case SVG_PATH_CMD_CLOSE_UPPER:
            path_ls << "CloseUpper" << path_ls.comma;
            break;
          case SVG_PATH_CMD_CLOSE:
            path_ls << "Close" << path_ls.comma;
            break;

          case SVG_PATH_CMD_LINE_TO:
            path_ls << "LineTo"
                    << "(x:" << cmd->line_to.x
                    << ",y:" << cmd->line_to.y
                    << ")" << path_ls.comma;
            break;
          case SVG_PATH_CMD_LINE_TO_REL:
            path_ls << "LineToRel"
                    << "(dx:" << cmd->line_to.x
                    << ",dy:" << cmd->line_to.y
                    << ")" << path_ls.comma;
            break;

          case SVG_PATH_CMD_HLINE_TO:
            path_ls << "HLineTo"
                    << "(x:" << cmd->hline_to.c
                    << ")" << path_ls.comma;
            break;
          case SVG_PATH_CMD_HLINE_TO_REL:
            path_ls << "HLineToRel"
                    << "(dx:" << cmd->hline_to.c
                    << ")" << path_ls.comma;
            break;

          case SVG_PATH_CMD_VLINE_TO:
            path_ls << "VLineTo"
                    << "(y:" << cmd->vline_to.c
                    << ")" << path_ls.comma;
            break;
          case SVG_PATH_CMD_VLINE_TO_REL:
            path_ls << "VLineToRel"
                    << "(dy:" << cmd->vline_to.c
                    << ")" << path_ls.comma;
            break;

          case SVG_PATH_CMD_CUBIC_TO:
            path_ls << "CubicTo"
                    << "(x1:" << cmd->cubic_to.x1
                    << ",y1:" << cmd->cubic_to.y1
                    << ",x2:" << cmd->cubic_to.x2
                    << ",y2:" << cmd->cubic_to.y2
                    << ",x:" << cmd->cubic_to.x
                    << ",y:" << cmd->cubic_to.y
                    << ")" << path_ls.comma;
            break;

          case SVG_PATH_CMD_CUBIC_TO_REL:
            path_ls << "CubicToRel"
                    << "(dx1:" << cmd->cubic_to.x1
                    << ",dy1:" << cmd->cubic_to.y1
                    << ",dx2:" << cmd->cubic_to.x2
                    << ",dy2:" << cmd->cubic_to.y2
                    << ",dx:" << cmd->cubic_to.x
                    << ",dy:" << cmd->cubic_to.y
                    << ")" << path_ls.comma;
            break;

          case SVG_PATH_CMD_CUBIC_SMOOTH_TO:
            path_ls << "CubicSmoothTo"
                    << "(x2:" << cmd->cubic_smooth_to.x2
                    << ",y2:" << cmd->cubic_smooth_to.y2
                    << ",x:" << cmd->cubic_smooth_to.x
                    << ",y:" << cmd->cubic_smooth_to.y
                    << ")" << path_ls.comma;
            break;
          case SVG_PATH_CMD_CUBIC_SMOOTH_TO_REL:
            path_ls << "CubicSmoothToRel"
                    << "(dx2:" << cmd->cubic_smooth_to.x2
                    << ",dy2:" << cmd->cubic_smooth_to.y2
                    << ",dx:" << cmd->cubic_smooth_to.x
                    << ",dy:" << cmd->cubic_smooth_to.y
                    << ")" << path_ls.comma;
            break;

          case SVG_PATH_CMD_QUAD_TO:
            path_ls << "QuadTo"
                    << "(x1:" << cmd->quad_to.x1
                    << ",y1:" << cmd->quad_to.y1
                    << ",x:" << cmd->quad_to.x
                    << ",y:" << cmd->quad_to.y
                    << ")" << path_ls.comma;
            break;
          case SVG_PATH_CMD_QUAD_TO_REL:
            path_ls << "QuadToRel"
                    << "(dx1:" << cmd->quad_to.x1
                    << ",dy1:" << cmd->quad_to.y1
                    << ",dx:" << cmd->quad_to.x
                    << ",dy:" << cmd->quad_to.y
                    << ")" << path_ls.comma;
            break;

          case SVG_PATH_CMD_QUAD_SMOOTH_TO:
            path_ls << "QuadSmoothTo"
                    << "(x:" << cmd->quad_to.x
                    << ",y:" << cmd->quad_to.y
                    << ")" << path_ls.comma;
            break;
          case SVG_PATH_CMD_QUAD_SMOOTH_TO_REL:
            path_ls << "QuadSmoothToRel"
                    << "(dx:" << cmd->quad_to.x
                    << ",dy:" << cmd->quad_to.y
                    << ")" << path_ls.comma;
            break;

          case SVG_PATH_CMD_RAT_CUBIC_TO:
            path_ls << "RatCubicTo"
                    << "(x1:" << cmd->rat_cubic_to.x1
                    << ",y1:" << cmd->rat_cubic_to.y1
                    << ",x2:" << cmd->rat_cubic_to.x2
                    << ",y2:" << cmd->rat_cubic_to.y2
                    << ",x:" << cmd->rat_cubic_to.x
                    << ",y:" << cmd->rat_cubic_to.y
                    << ",w1:" << cmd->rat_cubic_to.w1
                    << ",w2:" << cmd->rat_cubic_to.w2
                    << ")" << path_ls.comma;
            break;

          case SVG_PATH_CMD_RAT_CUBIC_TO_REL:
            path_ls << "RatCubicToRel"
                    << "(dx1:" << cmd->rat_cubic_to.x1
                    << ",dy1:" << cmd->rat_cubic_to.y1
                    << ",w1:" << cmd->rat_cubic_to.w1
                    << ",dx2:" << cmd->rat_cubic_to.x2
                    << ",dy2:" << cmd->rat_cubic_to.y2
                    << ",w2:" << cmd->rat_cubic_to.w2
                    << ",dx:" << cmd->rat_cubic_to.x
                    << ",dy:" << cmd->rat_cubic_to.y
                    << ",w1:" << cmd->rat_cubic_to.w1
                    << ",w2:" << cmd->rat_cubic_to.w2
                    << ")" << path_ls.comma;
            break;

          case SVG_PATH_CMD_RAT_QUAD_TO:
            path_ls << "RatQuadTo"
                    << "(x1:" << cmd->rat_quad_to.x1
                    << ",y1:" << cmd->rat_quad_to.y1
                    << ",x:" << cmd->rat_quad_to.x
                    << ",y:" << cmd->rat_quad_to.y
                    << ",w1:" << cmd->rat_quad_to.w1
                    << ")" << path_ls.comma;
            break;
          case SVG_PATH_CMD_RAT_QUAD_TO_REL:
            path_ls << "RatQuadToRel"
                    << "(dx1:" << cmd->rat_quad_to.x1
                    << ",dy1:" << cmd->rat_quad_to.y1
                    << ",dx:" << cmd->rat_quad_to.x
                    << ",dy:" << cmd->rat_quad_to.y
                    << ",w1:" << cmd->rat_quad_to.w1
                    << ")" << path_ls.comma;
            break;

          case SVG_PATH_CMD_ARC_TO:
            path_ls << "ArcTo"
                    << "(rx:" << cmd->arc_to.rx
                    << ",ry:" << cmd->arc_to.ry
                    << ",x:"  << cmd->arc_to.x
                    << ",y:"  << cmd->arc_to.y
                    << ",x_axis_rotation:" << cmd->arc_to.x_axis_rotation
                    << ".swwp_flag:" << cmd->arc_to.sweep_flag
                    << ")" << path_ls.comma;
            break;
          case SVG_PATH_CMD_ARC_TO_REL:
            path_ls << "ArcToRel"
                    << "(rx:" << cmd->arc_to.rx
                    << ",ry:" << cmd->arc_to.ry
                    << ",dx:"  << cmd->arc_to.x
                    << ",dy:"  << cmd->arc_to.y
                    << ",x_axis_rotation:" << cmd->arc_to.x_axis_rotation
                    << ".swee_flag:" << cmd->arc_to.sweep_flag
                    << ")" << path_ls.comma;
            break;
          }
          // clang-format on
        }

      svg_path_iterator_dispose(iterator);

      p.print(ss.str());
      p.print("\n");
    }

  //
  // Rasters
  //
  for (uint32_t raster_index = 0; raster_index < raster_count; ++raster_index)
    {
      p.print("  raster[%u]: ", raster_index);
      svg_raster_iterator * iterator = svg_raster_iterator_create(svg, raster_index);

      // The real raster index only appears in the final SVG_RASTER_CMD_END
      // item and will be stored here.
      uint32_t          end_raster_index = UINT32_MAX;
      std::stringstream ss;
      list_ostream      ls(ss);

      const union svg_raster_cmd * cmd;
      while (svg_raster_iterator_next(iterator, &cmd))
        {
          // clang-format off
          switch (cmd->type) {
          case SVG_RASTER_CMD_BEGIN:
            // Simple marker, nothing to do.
            break;

          case SVG_RASTER_CMD_END:
            // End marker. Contains the final raster index.
            end_raster_index = cmd->end.raster_index;
            break;

          case SVG_RASTER_CMD_FILL:
            ls << "Fill(path:" << cmd->fill.path_index << ")" << ls.comma;
            break;

          case SVG_RASTER_CMD_STROKE:
            ls << "Stroke(path:" << cmd->stroke.path_index << ")" << ls.comma;
            break;

          case SVG_RASTER_CMD_MARKER:
            ls << "Marker(path:" << cmd->marker.path_index << ")" << ls.comma;
            break;

          case SVG_RASTER_CMD_STROKE_WIDTH:
            ls << "StrokeWidth"
              << "(w:" << cmd->stroke_width.stroke_width
              << ")" << ls.comma;
            break;

          case SVG_RASTER_CMD_TRANSFORM_PROJECT:
            ls << "Transform(sx:" << cmd->project.sx;
            if (cmd->project.shx)
              ls << ",shx:" << cmd->project.shx;
            ls << ",sy:" << cmd->project.sy;
            if (cmd->project.shy)
              ls << ",shy:" << cmd->project.shy;
            if (cmd->project.tx || cmd->project.ty)
              ls << ",tx:" << cmd->project.tx << ",ty:" << cmd->project.ty;
            // always print these
            ls << ",w0:" << cmd->project.w0 << ",w1:" << cmd->project.w1;
            ls << ")" << ls.comma;
            break;

          case SVG_RASTER_CMD_TRANSFORM_MATRIX:
            ls << "Transform(sx:" << cmd->matrix.sx;
            if (cmd->matrix.shx)
              ls << ",shx:" << cmd->matrix.shx;
            ls << ",sy:" << cmd->matrix.sy;
            if (cmd->matrix.shy)
              ls << ",shy:" << cmd->matrix.shy;
            if (cmd->matrix.tx || cmd->matrix.ty)
              ls << ",tx:" << cmd->matrix.tx << ",ty:" << cmd->matrix.ty;
            ls << ")" << ls.comma;
            break;

          case SVG_RASTER_CMD_TRANSFORM_TRANSLATE:
            ls << "Translate"
              << "(tx:" << cmd->translate.tx
              << ",ty:" << cmd->translate.ty
              << ")" << ls.comma;
            break;

          case SVG_RASTER_CMD_TRANSFORM_SCALE:
            ls << "Scale"
              << "(sx:" << cmd->scale.sx
              << ",sy:" << cmd->scale.sy
              << ")" << ls.comma;
            break;

          case SVG_RASTER_CMD_TRANSFORM_ROTATE:
            ls << "Rotate"
              << "(d:" << cmd->rotate.d;
            if (cmd->rotate.cx || cmd->rotate.cy) {
              ls << ",cx:" << cmd->rotate.cx
                 << ",cy:" << cmd->rotate.cy;
            }
            ls << ")" << ls.comma;
            break;

          case SVG_RASTER_CMD_TRANSFORM_SKEW_X:
            ls << "SkewX"
              << "(d:" << cmd->skew_x.d
              << ")" << ls.comma;
            break;

          case SVG_RASTER_CMD_TRANSFORM_SKEW_Y:
            ls << "SkewY"
              << "(d:" << cmd->skew_y.d
              << ")" << ls.comma;
            break;

          case SVG_RASTER_CMD_TRANSFORM_DROP:
            ls << "Drop" << ls.comma;
            break;
          }
          // clang-format on
        }

      if (end_raster_index != raster_index)
        {
          ls << "RasterIndex(" << end_raster_index << ")";
        }

      svg_raster_iterator_dispose(iterator);
      p.print(ss.str());
      p.print("\n");
    }

  //
  // Layers
  //
  for (uint32_t layer_index = 0; layer_index < layer_count; ++layer_index)
    {
      p.print("  layer[%u]: ", layer_index);

      std::stringstream ss;
      list_ostream      ls(ss);

      svg_layer_iterator *        iterator = svg_layer_iterator_create(svg, layer_index);
      const union svg_layer_cmd * cmd;

      uint32_t begin_layer_index = UINT32_MAX;
      while (svg_layer_iterator_next(iterator, &cmd))
        {
          // clang-format off
          switch (cmd->type)
          {
          case SVG_LAYER_CMD_BEGIN:
            begin_layer_index = cmd->begin.layer_index;
            if (begin_layer_index != layer_index)
              ls << "LayerIndex(" << begin_layer_index << ")" << ls.comma;
            break;

          case SVG_LAYER_CMD_END:
            // Simpler marker, nothing to do.
            break;

          case SVG_LAYER_CMD_PLACE:
            ls << "Place"
              << "(raster:" << cmd->place.raster_index
              << ",tx:" << cmd->place.tx
              << ",ty:" << cmd->place.ty
              << ")" << ls.comma;
            break;

          case SVG_LAYER_CMD_OPACITY:
            ls << "Opacity(" << cmd->opacity.opacity << ")" << ls.comma;
            break;

          case SVG_LAYER_CMD_FILL_RULE:
            ls << "FillRule("
              << fillRuleToString(cmd->fill_rule.fill_rule)
              << ")" << ls.comma;
            break;

          case SVG_LAYER_CMD_FILL_COLOR:
            ls << "FillColor("
              << colorToString(cmd->fill_color.fill_color)
              << ")" << ls.comma;
            break;

          case SVG_LAYER_CMD_FILL_OPACITY:
            ls << "FillOpacity("
              << cmd->fill_opacity.fill_opacity
              << ")" << ls.comma;

          case SVG_LAYER_CMD_STROKE_COLOR:
            ls << "StrokeColor("
              << colorToString(cmd->stroke_color.stroke_color)
              << ")" << ls.comma;
            break;

          case SVG_LAYER_CMD_STROKE_OPACITY:
            ls << "StrokeOpacity("
              << cmd->stroke_opacity.stroke_opacity
              << ")" << ls.comma;
            break;
          }
          // clang-format on
        }
      svg_layer_iterator_dispose(iterator);

      p.print(ss.str());
      p.print("\n");
    }

  p.print("}\n");
}

void
svg_print_stdout(const struct svg * svg)
{
  svg_print(svg, reinterpret_cast<svg_printf_func_t *>(vfprintf), stdout);
}

struct PrintBuffer
{
  PrintBuffer(std::ostream & os) : buffer_(buffer0_), os_(os)
  {
  }

  ~PrintBuffer()
  {
    reset();
  }

  void
  reset()
  {
    if (buffer_ != buffer0_)
      {
        free(buffer_);
        buffer_   = buffer0_;
        capacity_ = kDefaultCapacity;
      }
  }

  void
  print(const char * format, va_list args)
  {
    for (;;)
      {
        // Try to print in current buffer and print it.
        int len = vsnprintf(buffer_, capacity_, format, args);
        if (len >= 0 && static_cast<size_t>(len) < capacity_)
          {
            os_ << buffer_;
            return;
          }
        // On failure (i.e. truncation), double capacity and loop over.
        char * old_buffer   = (buffer_ == buffer0_) ? nullptr : buffer_;
        size_t new_capacity = capacity_ * 2;
        buffer_             = (char *)realloc(old_buffer, new_capacity);
        capacity_           = new_capacity;
      }
  }

  static void
  print_func(void * opaque, const char * fmt, va_list args)
  {
    reinterpret_cast<PrintBuffer *>(opaque)->print(fmt, args);
  }

 private:
  static const size_t kDefaultCapacity = 511;
  size_t              capacity_        = kDefaultCapacity;
  char *              buffer_;
  char                buffer0_[kDefaultCapacity + 1];
  std::ostream &      os_;
};

std::ostream &
operator<<(std::ostream & os, const svg * svg)
{
  PrintBuffer buf(os);
  svg_print(svg, buf.print_func, &buf);
  return os;
}
