// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SVG_SVG_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SVG_SVG_H_

//
//
//

#include <stdbool.h>
#include <stdint.h>

//
// This is a minimalist SVG parser that creates a representation of the
// input document as three parallel lists of the following types:
//
//   - Path items:
//       Corresponding to SVG path/shape elements as they appear in the
//       document (i.e. without any transforms applied).
//
//   - Raster items:
//       Corresponding to rasterization attributes of the paths above (i.e.
//       transforms or path fill / path stroke operations).
//
//   - Layer items:
//       Corresponding to presentation attributes (e.g. color, opacity, fill
//       rule) of the rasters above.
//
// Each item is stored as an array of simple tagged structs describing
// 'commands' from the input SVG document. See the definitions of
// svg_{path,raster,layer}_cmd below for details.
//
// The number of items in each list can be retrieved by calling
// svg_{path,raster,layer}_count(), and each individual item can be enumerated
// (as a series of commands) with svg_iterator_create_for_{path,raster,layer}().
//
// Each list stores its items' in a single concatenated array of commands in
// memory. This allows enumerating the whole list as a series of commands too,
// since each item type has specific 'begin' and 'end' tags.
// See svg_iterator_create_for_all_{paths,rasters,layers}().
//
// The following table lists which SVG elements are associated with the
// path, raster and layer lists:
//
// element attributes : id
// container elements : svg, g
// path elements      : circle, ellipse, line, path, polygon, polyline, rect
// raster attributes  : transform, fill|stroke|marker, style props (*)
// layer attributes   : fill-rule, opacities, colors or gradient references, style props (*)
//
// See svg2spinel.[c|h] for an example of how to decode the commands.
//
// Limitations:
//
// The parser can parse basic SVG docs and has the following
// limitations:
//
//   - Doesn't support CSS styling
//   - Doesn't support hrefs
//   - Doesn't support text
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

struct svg;

union svg_path_cmd;
union svg_raster_cmd;
union svg_layer_cmd;

//
// Open and parse an SVG doc from a filename.
//
// File I/O and parsing errors result in an error message and NULL is
// returned.
//

struct svg *
svg_open(char const * const filename, bool const is_verbose);

//
// Parse an SVG doc from a zero-terminated byte array.
//
// Parsing errors result in an error message and NULL is returned.
//

struct svg *
svg_parse(char const * doc, const bool is_verbose);

//
// Dispose of the SVG doc.
//

void
svg_dispose(struct svg * const sd);

//
// Return the number of entries in each list.
//

extern uint32_t
svg_path_count(struct svg const * const sd);

extern uint32_t
svg_raster_count(struct svg const * const sd);

extern uint32_t
svg_layer_count(struct svg const * const sd);

//
// Path iteration
//

struct svg_path_iterator;

// Create new iterator to loop over all commands of a given path, or if
// |path_index| is UINT32_MAX, all path commands in the document.
extern struct svg_path_iterator *
svg_path_iterator_create(struct svg const * const sd, uint32_t const path_index);

// Return next path command. Return false at the end of the list,
// otherwise set |*cmd| and return true.
extern bool
svg_path_iterator_next(struct svg_path_iterator * iterator, union svg_path_cmd const ** cmd);

// Destroy a path iterator instance.
extern void
svg_path_iterator_dispose(struct svg_path_iterator * iterator);

//
// Raster iteration
//

struct svg_raster_iterator;

// Create new iterator to loop over all commands of a given raster, or if
// |raster_index| is UINT32_MAX, all raster commands in the document.
extern struct svg_raster_iterator *
svg_raster_iterator_create(struct svg const * const sd, uint32_t const raster_index);

extern bool
svg_raster_iterator_next(struct svg_raster_iterator * iterator, union svg_raster_cmd const ** cmd);

// Destroy a raster iterator instance.
extern void
svg_raster_iterator_dispose(struct svg_raster_iterator * iterator);

//
// Layer iteration
//

struct svg_layer_iterator;

extern struct svg_layer_iterator *
svg_layer_iterator_create(struct svg const * const sd, uint32_t const layer_index);

extern bool
svg_layer_iterator_next(struct svg_layer_iterator * iterator, union svg_layer_cmd const ** cmd);

extern void
svg_layer_iterator_dispose(struct svg_layer_iterator * iterator);

//
// SVG COLOR
//
// An SVG color is stored as a big-endian R8:G8:B8 value.
//
//   0   8  16  24
//   +---+---+---+
//   | B | G | R |
//   +---+---+---+
//

typedef uint32_t svg_color_t;

//
// PATH COMMAND TYPES
//

typedef enum svg_path_cmd_type
{
  //
  SVG_PATH_CMD_BEGIN,
  SVG_PATH_CMD_END,

  // SVG PATH OBJECTS
  SVG_PATH_CMD_CIRCLE,
  SVG_PATH_CMD_ELLIPSE,
  SVG_PATH_CMD_LINE,
  SVG_PATH_CMD_POLYGON,
  SVG_PATH_CMD_POLYLINE,
  SVG_PATH_CMD_RECT,

  // POLY POINT
  SVG_PATH_CMD_POLY_POINT,
  SVG_PATH_CMD_POLY_END,

  // GEOMETRY COMMANDS
  SVG_PATH_CMD_PATH_BEGIN,
  SVG_PATH_CMD_PATH_END,

  SVG_PATH_CMD_MOVE_TO,
  SVG_PATH_CMD_MOVE_TO_REL,

  SVG_PATH_CMD_CLOSE_UPPER,
  SVG_PATH_CMD_CLOSE,

  SVG_PATH_CMD_LINE_TO,
  SVG_PATH_CMD_LINE_TO_REL,

  SVG_PATH_CMD_HLINE_TO,
  SVG_PATH_CMD_HLINE_TO_REL,

  SVG_PATH_CMD_VLINE_TO,
  SVG_PATH_CMD_VLINE_TO_REL,

  SVG_PATH_CMD_CUBIC_TO,
  SVG_PATH_CMD_CUBIC_TO_REL,

  SVG_PATH_CMD_CUBIC_SMOOTH_TO,
  SVG_PATH_CMD_CUBIC_SMOOTH_TO_REL,

  SVG_PATH_CMD_QUAD_TO,
  SVG_PATH_CMD_QUAD_TO_REL,

  SVG_PATH_CMD_QUAD_SMOOTH_TO,
  SVG_PATH_CMD_QUAD_SMOOTH_TO_REL,

  SVG_PATH_CMD_ARC_TO,
  SVG_PATH_CMD_ARC_TO_REL,
} svg_path_cmd_type;

//
// PATH COMMAND STRUCTS
//

struct svg_path_point
{
  float x;
  float y;
};

//

struct svg_path_cmd_begin
{
  svg_path_cmd_type type;
};

struct svg_path_cmd_end
{
  svg_path_cmd_type type;
  uint32_t          path_index;
};

struct svg_path_cmd_circle
{
  svg_path_cmd_type type;
  float             cx;
  float             cy;
  float             r;
};

struct svg_path_cmd_ellipse
{
  svg_path_cmd_type type;
  float             cx;
  float             cy;
  float             rx;
  float             ry;
};

struct svg_path_cmd_line
{
  svg_path_cmd_type type;
  float             x1;
  float             y1;
  float             x2;
  float             y2;
};

struct svg_path_cmd_polygon
{
  svg_path_cmd_type type;
};

struct svg_path_cmd_polyline
{
  svg_path_cmd_type type;
};

struct svg_path_cmd_poly_point
{
  svg_path_cmd_type type;
  float             x;
  float             y;
};

struct svg_path_cmd_poly_end
{
  svg_path_cmd_type type;
};

struct svg_path_cmd_rect
{
  svg_path_cmd_type type;
  float             x;
  float             y;
  float             width;
  float             height;
  float             rx;
  float             ry;
};

struct svg_path_cmd_path_begin
{
  svg_path_cmd_type type;
};

struct svg_path_cmd_path_end
{
  svg_path_cmd_type type;
};

struct svg_path_cmd_move_to
{
  svg_path_cmd_type type;
  float             x;
  float             y;
};

struct svg_path_cmd_close
{
  svg_path_cmd_type type;
};

struct svg_path_cmd_line_to
{
  svg_path_cmd_type type;
  float             x;
  float             y;
};

struct svg_path_cmd_coord_to
{
  svg_path_cmd_type type;
  float             c;
};

struct svg_path_cmd_hline_to
{
  svg_path_cmd_type type;
  float             c;
};

struct svg_path_cmd_vline_to
{
  svg_path_cmd_type type;
  float             c;
};

struct svg_path_cmd_cubic_to
{
  svg_path_cmd_type type;
  float             x1;
  float             y1;
  float             x2;
  float             y2;
  float             x;
  float             y;
};

struct svg_path_cmd_cubic_smooth_to
{
  svg_path_cmd_type type;
  float             x2;
  float             y2;
  float             x;
  float             y;
};

struct svg_path_cmd_quad_to
{
  svg_path_cmd_type type;
  float             x1;
  float             y1;
  float             x;
  float             y;
};

struct svg_path_cmd_quad_smooth_to
{
  svg_path_cmd_type type;
  float             x;
  float             y;
};

struct svg_path_cmd_arc_to
{
  svg_path_cmd_type type;
  float             rx;
  float             ry;
  float             x_axis_rotation;
  float             large_arc_flag;
  float             sweep_flag;
  float             x;
  float             y;
};

//
//
//

union svg_path_cmd
{
  svg_path_cmd_type type;

  struct svg_path_cmd_begin           begin;
  struct svg_path_cmd_end             end;
  struct svg_path_cmd_circle          circle;
  struct svg_path_cmd_ellipse         ellipse;
  struct svg_path_cmd_line            line;
  struct svg_path_cmd_polygon         polygon;
  struct svg_path_cmd_polyline        polyline;
  struct svg_path_cmd_poly_point      poly_point;
  struct svg_path_cmd_rect            rect;
  struct svg_path_cmd_path_begin      path_begin;
  struct svg_path_cmd_path_end        path_end;
  struct svg_path_cmd_move_to         move_to;
  struct svg_path_cmd_close           close;
  struct svg_path_cmd_line_to         line_to;
  struct svg_path_cmd_hline_to        hline_to;
  struct svg_path_cmd_vline_to        vline_to;
  struct svg_path_cmd_cubic_to        cubic_to;
  struct svg_path_cmd_cubic_smooth_to cubic_smooth_to;
  struct svg_path_cmd_quad_to         quad_to;
  struct svg_path_cmd_quad_smooth_to  quad_smooth_to;
  struct svg_path_cmd_arc_to          arc_to;
};

//
// RASTER COMMAND TYPES
//

typedef enum svg_raster_cmd_type
{
  //
  SVG_RASTER_CMD_BEGIN,
  SVG_RASTER_CMD_END,

  // RASTERIZE PATH
  SVG_RASTER_CMD_FILL,
  SVG_RASTER_CMD_STROKE,
  SVG_RASTER_CMD_MARKER,

  //
  SVG_RASTER_CMD_STROKE_WIDTH,

  // TRANSFORM PATH BEFORE RASTERIZING
  SVG_RASTER_CMD_TRANSFORM_MATRIX,     // 6 float
  SVG_RASTER_CMD_TRANSFORM_TRANSLATE,  // 2 float
  SVG_RASTER_CMD_TRANSFORM_SCALE,      // 2 float
  SVG_RASTER_CMD_TRANSFORM_ROTATE,     // 3 float
  SVG_RASTER_CMD_TRANSFORM_SKEW_X,     // 1 float
  SVG_RASTER_CMD_TRANSFORM_SKEW_Y,     // 1 float

  // DROP TRANSFORM FROM HOST'S TRANSFORM STACK
  SVG_RASTER_CMD_TRANSFORM_DROP

} svg_raster_cmd_type;

//
// RASTER COMMAND STRUCTS
//

struct svg_raster_cmd_begin
{
  svg_raster_cmd_type type;
};

struct svg_raster_cmd_end
{
  svg_raster_cmd_type type;
  uint32_t            raster_index;
};

struct svg_raster_cmd_fsm
{
  svg_raster_cmd_type type;
  uint32_t            path_index;
};

struct svg_raster_cmd_fill
{
  svg_raster_cmd_type type;
  uint32_t            path_index;
};

struct svg_raster_cmd_stroke
{
  svg_raster_cmd_type type;
  uint32_t            path_index;
};

struct svg_raster_cmd_marker
{
  svg_raster_cmd_type type;
  uint32_t            path_index;
};

struct svg_raster_cmd_stroke_width
{
  svg_raster_cmd_type type;
  float               stroke_width;
};

struct svg_raster_cmd_transform_matrix
{
  svg_raster_cmd_type type;
  float               sx;
  float               shy;
  float               shx;
  float               sy;
  float               tx;
  float               ty;
};

struct svg_raster_cmd_transform_translate
{
  svg_raster_cmd_type type;
  float               tx;
  float               ty;
};

struct svg_raster_cmd_transform_scale
{
  svg_raster_cmd_type type;
  float               sx;
  float               sy;
};

struct svg_raster_cmd_transform_rotate
{
  svg_raster_cmd_type type;
  float               d;
  float               cx;
  float               cy;
};

struct svg_raster_cmd_transform_skew_x
{
  svg_raster_cmd_type type;
  float               d;
};

struct svg_raster_cmd_transform_skew_y
{
  svg_raster_cmd_type type;
  float               d;
};

struct svg_raster_cmd_transform_drop
{
  svg_raster_cmd_type type;
};

//
//
//

union svg_raster_cmd
{
  svg_raster_cmd_type type;

  struct svg_raster_cmd_begin               begin;
  struct svg_raster_cmd_end                 end;
  struct svg_raster_cmd_fsm                 fsm;
  struct svg_raster_cmd_fill                fill;
  struct svg_raster_cmd_stroke              stroke;
  struct svg_raster_cmd_marker              marker;
  struct svg_raster_cmd_stroke_width        stroke_width;  // stroke_* attributes
  struct svg_raster_cmd_transform_matrix    matrix;
  struct svg_raster_cmd_transform_translate translate;
  struct svg_raster_cmd_transform_scale     scale;
  struct svg_raster_cmd_transform_rotate    rotate;
  struct svg_raster_cmd_transform_skew_x    skew_x;
  struct svg_raster_cmd_transform_skew_y    skew_y;
  struct svg_raster_cmd_transform_drop      drop;
};

//
// LAYER COMMAND TYPES
//

typedef enum svg_fill_rule_op
{
  SVG_FILL_RULE_OP_EVENODD,
  SVG_FILL_RULE_OP_NONZERO
} svg_fill_rule_op;

//

typedef enum svg_layer_cmd_type
{
  //
  SVG_LAYER_CMD_BEGIN,
  SVG_LAYER_CMD_END,

  // PLACE RASTER ON LAYER
  SVG_LAYER_CMD_PLACE,  // { rasterId index tx ty }

  // LAYER PAINT SETTINGS
  SVG_LAYER_CMD_OPACITY,

  SVG_LAYER_CMD_FILL_RULE,
  SVG_LAYER_CMD_FILL_COLOR,
  SVG_LAYER_CMD_FILL_OPACITY,

  SVG_LAYER_CMD_STROKE_COLOR,
  SVG_LAYER_CMD_STROKE_OPACITY,

} svg_layer_cmd_type;

//
// LAYER COMMAND STRUCTS
//

struct svg_layer_cmd_begin
{
  svg_layer_cmd_type type;
  uint32_t           layer_index;
};
struct svg_layer_cmd_end
{
  svg_layer_cmd_type type;
};

struct svg_layer_cmd_place
{
  svg_layer_cmd_type type;
  uint32_t           raster_index;
  int                tx;
  int                ty;
};

struct svg_layer_cmd_opacity
{
  svg_layer_cmd_type type;
  float              opacity;
};

struct svg_layer_cmd_fill_rule
{
  svg_layer_cmd_type type;
  svg_fill_rule_op   fill_rule;
};

struct svg_layer_cmd_fill_color
{
  svg_layer_cmd_type type;
  svg_color_t        fill_color;
};

struct svg_layer_cmd_fill_opacity
{
  svg_layer_cmd_type type;
  float              fill_opacity;
};

struct svg_layer_cmd_stroke_color
{
  svg_layer_cmd_type type;
  svg_color_t        stroke_color;
};

struct svg_layer_cmd_stroke_opacity
{
  svg_layer_cmd_type type;
  float              stroke_opacity;
};

//
//
//

union svg_layer_cmd
{
  svg_layer_cmd_type type;

  struct svg_layer_cmd_begin          begin;
  struct svg_layer_cmd_end            end;
  struct svg_layer_cmd_place          place;
  struct svg_layer_cmd_opacity        opacity;
  struct svg_layer_cmd_fill_rule      fill_rule;
  struct svg_layer_cmd_fill_color     fill_color;
  struct svg_layer_cmd_fill_opacity   fill_opacity;
  struct svg_layer_cmd_stroke_color   stroke_color;
  struct svg_layer_cmd_stroke_opacity stroke_opacity;
};

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SVG_SVG_H_
