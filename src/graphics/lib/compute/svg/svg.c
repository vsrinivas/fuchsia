// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svg.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yxml.h>

#include "common/macros.h"
#include "svg_color_names.h"

//
//                 |  path  | raster | layer  |
// -NAMES----------+--------+--------+--------+
//                 |        |        |        |
// id              |   X    |   X    |   X    |
//                 |        |        |        |
// -CONTAINERS-----+--------+--------+--------+
//                 |        |        |        |
// svg             |   X    |   X    |   X    |
// g               |        |        |        |
//                 |        |        |        |
// -P ELEMENTS-----+--------+--------+--------+
//                 |        |        |        |
// circle          |        |        |        |
// ellipse         |        |        |        |
// line            |        |        |        |
// path            |        |        |        |
// polygon         |        |        |        |
// polyline        |        |        |        |
// rect            |        |        |        |
//                 |        |        |        |
// -P ATTRIBUTES---+--------+--------+--------+
//                 |        |        |        |
// r               |        |        |        | circle
// cx              |        |        |        | circle, ellipse
// cy              |        |        |        | circle, ellipse
// rx              |        |        |        | rect,   ellipse
// ry              |        |        |        | rect,   ellipse
// x               |        |        |        | rect
// y               |        |        |        | rect
// width           |        |        |        | rect
// height          |        |        |        | rect
// x1              |        |        |        | line
// y1              |        |        |        | line
// x2              |        |        |        | line
// y2              |        |        |        | line
//                 |        |        |        |
// d               |        |        |        | path
// points          |        |        |        | polygon, polyline
//                 |        |        |        |
// -R ATTRIBUTES---+--------+--------+--------+
//                 |        |        |        |
// transform    <> |   X    |   X    |   o    |
// fill|stroke  <> |   X    |   X    |   o    | <-- defined by change to either paint-ops or paint-op colors
// stroke-width <> |   X    |   X    |   o    |
//                 |        |        |        |
// -L ATTRIBUTES---+--------+--------+--------+
//                 |        |        |        |
// opacity         |   X    |   X    |   X    |
// fill-rule       |   X    |   X    |   X    |
// fill-color   <> |   X    |   X    |   X    |
// fill-opacity    |   X    |   X    |   X    |
// stroke-color <> |   X    |   X    |   X    |
// stroke-opacity  |   X    |   X    |   X    |
//                 |        |        |        |
// ----------------+--------+--------+--------+
//

//
//  NAME
//  - starting indices in dictionaries
//  - base indices for path/raster/layer arrays
//
//  PATH
//  - create path
//  - ( path commands )+
//  - seal path @ pathId array index
//
//  RASTER
//  - ( push raster attributes )* <-- most likely hanging off of svg/g/- container
//  - create raster
//  - (
//  -   ( push raster attributes               )*
//  -   ( add filled/stroke pathId array index )*
//  -   ( pop raster attributes                )*
//  - )+
//  - seal raster @ rasterId array index
//
//  LAYER
//  - ( push layer attributes )*  <-- most likely hanging off of svg/g/- container
//  - using current layer
//  - (
//  -   ( push layer attributes      )*
//  -   ( place rasterId array index )*
//  -   ( pop layer attributes       )*
//  - )+
//  - increment current layer index
//

//
// paths only need to be pulled once and can be reused
// only need to split off rasters if rerasterizing
// top-level "use" routine will rerasterize as needed
//
// acquire doc resources
// release doc resources
//
// acquire def resources
// release def resources
//
// render doc
// render def
//
// count paths
// pull all paths
// dispose all paths
// pull path by name
// dispose path by name
//
// count rasters
// rasterize all paths
// dispose all rasters
// rasterize by name
// dispose raster by name
//
// count layers
// acquire all layers
// release all layers
// acquire layer by name
// release layer by name
//

struct svg_parser;

//
// ATTRIBS
//

typedef void (*svg_parse_attrib_pfn)(struct svg_parser * sp,
                                     yxml_t *            ys,
                                     char *              val,
                                     uint32_t const      len);

struct svg_attrib
{
  char const *         name;
  svg_parse_attrib_pfn pfn;
};

static struct svg_attrib const *
svg_attrib_lookup(char const * str, uint32_t len);

//
// NOTE: STRINGS MUST BE IN ALPHABETICAL ORDER
//
// clang-format off
#define SVG_ATTRIBS_EXPAND(macro_)                            \
  macro_(cx,              svg_parse_attrib_cx)                \
  macro_(cy,              svg_parse_attrib_cy)                \
  macro_(d,               svg_parse_attrib_d)                 \
  macro_(fill,            svg_parse_attrib_fill_color)        \
  macro_(fill-opacity,    svg_parse_attrib_fill_opacity)      \
  macro_(fill-rule,       svg_parse_attrib_fill_rule)         \
  macro_(height,          svg_parse_attrib_height)            \
  macro_(id,              svg_parse_attrib_id)                \
  macro_(opacity,         svg_parse_attrib_opacity)           \
  macro_(points,          svg_parse_attrib_points)            \
  macro_(r,               svg_parse_attrib_r)                 \
  macro_(rx,              svg_parse_attrib_rx)                \
  macro_(ry,              svg_parse_attrib_ry)                \
  macro_(stroke,          svg_parse_attrib_stroke_color)      \
  macro_(stroke-opacity,  svg_parse_attrib_stroke_opacity)    \
  macro_(stroke-width,    svg_parse_attrib_stroke_width)      \
  macro_(style,           svg_parse_attrib_style)             \
  macro_(transform,       svg_parse_attrib_transform)         \
  macro_(width,           svg_parse_attrib_width)             \
  macro_(x,               svg_parse_attrib_x)                 \
  macro_(x1,              svg_parse_attrib_x1)                \
  macro_(x2,              svg_parse_attrib_x2)                \
  macro_(y,               svg_parse_attrib_y)                 \
  macro_(y1,              svg_parse_attrib_y1)                \
  macro_(y2,              svg_parse_attrib_y2)
// clang-format on

//
// TRANSFORMS
//

typedef void (*svg_parse_transform_pfn)(struct svg_parser * sp,
                                        yxml_t *            ys,
                                        char *              val,
                                        uint32_t const      len);

struct svg_transform
{
  char const *            name;
  svg_parse_transform_pfn pfn;
};

static struct svg_transform const *
svg_transform_lookup(char const * str, uint32_t len);

//
// NOTE: STRINGS MUST BE IN ALPHABETICAL ORDER
//
// clang-format off
#define SVG_TRANSFORMS_EXPAND(macro_)                 \
  macro_(matrix,    svg_parse_transform_matrix)       \
  macro_(project,   svg_parse_transform_project)      \
  macro_(rotate,    svg_parse_transform_rotate)       \
  macro_(scale,     svg_parse_transform_scale)        \
  macro_(skewX,     svg_parse_transform_skewX)        \
  macro_(skewY,     svg_parse_transform_skewY)        \
  macro_(translate, svg_parse_transform_translate)
// clang-format off

//
// ELEMS
//

typedef void (*svg_parse_elem_pfn)(struct svg_parser * sp, yxml_t * ys);

struct svg_elem
{
  char const *       name;
  svg_parse_elem_pfn pfn;
};

static struct svg_elem const *
svg_elem_lookup(char const * str, uint32_t len);

//
// NOTE: STRINGS MUST BE IN ALPHABETICAL ORDER
//
// clang-format off
#define SVG_ELEMS_EXPAND(macro_)               \
  macro_(circle,    svg_parse_elem_circle)     \
  macro_(ellipse,   svg_parse_elem_ellipse)    \
  macro_(g,         svg_parse_elem_g)          \
  macro_(line,      svg_parse_elem_line)       \
  macro_(path,      svg_parse_elem_path)       \
  macro_(polygon,   svg_parse_elem_polygon)    \
  macro_(polyline,  svg_parse_elem_polyline)   \
  macro_(rect,      svg_parse_elem_rect)       \
  macro_(svg,       svg_parse_elem_svg)
// clang-format on

//
//
//

struct svg_lookup_cmd
{
  char *   name;
  uint32_t size;
};

#define SVG_LOOKUP_CMD(e_, s_)                                                                     \
  {                                                                                                \
    "" #e_, sizeof(s_)                                                                             \
  }

static struct svg_lookup_cmd const path_lookup_cmds[] = {
  SVG_LOOKUP_CMD(SVG_PATH_CMD_BEGIN, struct svg_path_cmd_begin),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_END, struct svg_path_cmd_end),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_CIRCLE, struct svg_path_cmd_circle),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_ELLIPSE, struct svg_path_cmd_ellipse),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_LINE, struct svg_path_cmd_line),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_POLYGON, struct svg_path_cmd_polygon),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_POLYLINE, struct svg_path_cmd_polyline),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_RECT, struct svg_path_cmd_rect),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_POLY_POINT, struct svg_path_cmd_poly_point),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_POLY_END, struct svg_path_cmd_poly_end),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_PATH_BEGIN, struct svg_path_cmd_path_begin),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_PATH_END, struct svg_path_cmd_path_end),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_MOVE_TO, struct svg_path_cmd_move_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_MOVE_TO_REL, struct svg_path_cmd_move_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_CLOSE_UPPER, struct svg_path_cmd_close),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_CLOSE, struct svg_path_cmd_close),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_LINE_TO, struct svg_path_cmd_line_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_LINE_TO_REL, struct svg_path_cmd_line_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_HLINE_TO, struct svg_path_cmd_hline_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_HLINE_TO_REL, struct svg_path_cmd_hline_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_VLINE_TO, struct svg_path_cmd_vline_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_VLINE_TO_REL, struct svg_path_cmd_vline_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_CUBIC_TO, struct svg_path_cmd_cubic_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_CUBIC_TO_REL, struct svg_path_cmd_cubic_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_CUBIC_SMOOTH_TO, struct svg_path_cmd_cubic_smooth_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_CUBIC_SMOOTH_TO_REL, struct svg_path_cmd_cubic_smooth_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_QUAD_TO, struct svg_path_cmd_quad_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_QUAD_TO_REL, struct svg_path_cmd_quad_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_QUAD_SMOOTH_TO, struct svg_path_cmd_quad_smooth_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_QUAD_SMOOTH_TO_REL, struct svg_path_cmd_quad_smooth_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_RAT_CUBIC_TO, struct svg_path_cmd_rat_cubic_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_RAT_CUBIC_TO_REL, struct svg_path_cmd_rat_cubic_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_RAT_QUAD_TO, struct svg_path_cmd_rat_quad_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_RAT_QUAD_TO_REL, struct svg_path_cmd_rat_quad_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_ARC_TO, struct svg_path_cmd_arc_to),
  SVG_LOOKUP_CMD(SVG_PATH_CMD_ARC_TO_REL, struct svg_path_cmd_arc_to)
};

static struct svg_lookup_cmd const raster_lookup_cmds[] = {
  SVG_LOOKUP_CMD(SVG_RASTER_CMD_BEGIN, struct svg_raster_cmd_begin),
  SVG_LOOKUP_CMD(SVG_RASTER_CMD_END, struct svg_raster_cmd_end),
  SVG_LOOKUP_CMD(SVG_RASTER_CMD_FILL, struct svg_raster_cmd_fill),
  SVG_LOOKUP_CMD(SVG_RASTER_CMD_STROKE, struct svg_raster_cmd_stroke),
  SVG_LOOKUP_CMD(SVG_RASTER_CMD_MARKER, struct svg_raster_cmd_marker),
  SVG_LOOKUP_CMD(SVG_RASTER_CMD_STROKE_WIDTH, struct svg_raster_cmd_stroke_width),
  SVG_LOOKUP_CMD(SVG_RASTER_CMD_TRANSFORM_PROJECT, struct svg_raster_cmd_transform_project),
  SVG_LOOKUP_CMD(SVG_RASTER_CMD_TRANSFORM_MATRIX, struct svg_raster_cmd_transform_matrix),
  SVG_LOOKUP_CMD(SVG_RASTER_CMD_TRANSFORM_TRANSLATE, struct svg_raster_cmd_transform_translate),
  SVG_LOOKUP_CMD(SVG_RASTER_CMD_TRANSFORM_SCALE, struct svg_raster_cmd_transform_scale),
  SVG_LOOKUP_CMD(SVG_RASTER_CMD_TRANSFORM_ROTATE, struct svg_raster_cmd_transform_rotate),
  SVG_LOOKUP_CMD(SVG_RASTER_CMD_TRANSFORM_SKEW_X, struct svg_raster_cmd_transform_skew_x),
  SVG_LOOKUP_CMD(SVG_RASTER_CMD_TRANSFORM_SKEW_Y, struct svg_raster_cmd_transform_skew_y),
  SVG_LOOKUP_CMD(SVG_RASTER_CMD_TRANSFORM_DROP, struct svg_raster_cmd_transform_drop)
};

static struct svg_lookup_cmd const layer_lookup_cmds[] = {
  SVG_LOOKUP_CMD(SVG_LAYER_CMD_BEGIN, struct svg_layer_cmd_begin),
  SVG_LOOKUP_CMD(SVG_LAYER_CMD_END, struct svg_layer_cmd_end),
  SVG_LOOKUP_CMD(SVG_LAYER_CMD_PLACE, struct svg_layer_cmd_place),
  SVG_LOOKUP_CMD(SVG_LAYER_CMD_OPACITY, struct svg_layer_cmd_opacity),
  SVG_LOOKUP_CMD(SVG_LAYER_CMD_FILL_RULE, struct svg_layer_cmd_fill_rule),
  SVG_LOOKUP_CMD(SVG_LAYER_CMD_FILL_COLOR, struct svg_layer_cmd_fill_color),
  SVG_LOOKUP_CMD(SVG_LAYER_CMD_FILL_OPACITY, struct svg_layer_cmd_fill_opacity),
  SVG_LOOKUP_CMD(SVG_LAYER_CMD_STROKE_COLOR, struct svg_layer_cmd_stroke_color),
  SVG_LOOKUP_CMD(SVG_LAYER_CMD_STROKE_OPACITY, struct svg_layer_cmd_stroke_opacity)
};

//
//
//

typedef enum svg_paint_op
{
  SVG_PAINT_OP_NONE,
  SVG_PAINT_OP_COLOR,
  SVG_PAINT_OP_INHERIT
} svg_paint_op;

typedef enum svg_marker_op
{
  SVG_MARKER_OP_FALSE,  // no arg
  SVG_MARKER_OP_TRUE    // no arg
} svg_marker_op;

//
//
//

typedef enum svg_attrib_type
{

  //
  // SCALARS
  //
  SVG_ATTRIB_TYPE_ELEM_COUNT,

  SVG_ATTRIB_TYPE_OPACITY,

  SVG_ATTRIB_TYPE_FILL_OP,
  SVG_ATTRIB_TYPE_FILL_COLOR,
  SVG_ATTRIB_TYPE_FILL_OPACITY,
  SVG_ATTRIB_TYPE_FILL_RULE,

  SVG_ATTRIB_TYPE_STROKE_OP,
  SVG_ATTRIB_TYPE_STROKE_COLOR,
  SVG_ATTRIB_TYPE_STROKE_OPACITY,
  SVG_ATTRIB_TYPE_STROKE_WIDTH,

  SVG_ATTRIB_TYPE_SVG_MARKER_OP,
  SVG_ATTRIB_TYPE_MARKER_COLOR,

  SVG_ATTRIB_TYPE_SCALAR_COUNT,  // NUMBER OF SCALAR ATTRIBS

  //
  // STACKS
  //
  SVG_ATTRIB_TYPE_TRANSFORM,  // drop from transform stack
  SVG_ATTRIB_TYPE_ID          // drop id stack

} svg_attrib_type;

#define SVG_ATTRIB_TYPE_TO_MASK(t) (1u << t)

#define SVG_ATTRIB_TYPE_TO_MASK_BIT(t) SVG_ATTRIB_TYPE_TO_MASK(t)
#define SVG_ATTRIB_TYPE_TO_MASK_OFF(t) 0

//
// STACK STRUCTURE
//

struct svg_stack_entry
{
  uint32_t idx;
  uint32_t len;
};

struct svg_stack
{
  struct svg_stack_entry * entries;
  uint32_t                 entry_max;
  uint32_t                 entry_count;

  void *   buf;
  uint32_t buf_max;
  uint32_t buf_count;
};

//
//
//

typedef void (*svg_on_stack_drop_pfn)(void * v, uint32_t len, void * extra);
typedef void (*svg_on_stack_push_pfn)(void * v, uint32_t len, void * extra);

//
//
//

static void
svg_stack_reset(struct svg_stack * s)
{
  s->entry_count = 0;
  s->buf_count   = 0;
}

static struct svg_stack *
svg_stack_alloc(uint32_t const entry_max, uint32_t const buf_max)
{
  struct svg_stack * const s = malloc(sizeof(*s));

  s->entries   = malloc(sizeof(*s->entries) * entry_max);
  s->entry_max = entry_max;

  s->buf     = malloc(buf_max);
  s->buf_max = buf_max;

  return s;
}

static struct svg_stack *
svg_stack_create()
{
  struct svg_stack * s = svg_stack_alloc(4096 / sizeof(struct svg_stack_entry), 4096);

  svg_stack_reset(s);

  return s;
}

static void
svg_stack_dispose(struct svg_stack * s)
{
  if (s == NULL)
    return;

  free(s->entries);
  free(s->buf);
  free(s);
}

//
//
//

static uint32_t
svg_stack_entry_count(struct svg_stack * s)
{
  return s->entry_count;
}

#ifdef SVG_EXTRA_DEBUGGING

static uint32_t
svg_stack_buf_count(struct svg_stack * s)
{
  return s->buf_count;
}

#endif

//
//
//

static void
svg_stack_ensure(struct svg_stack * s, uint32_t entry_inc, unsigned buf_inc)
{
  uint32_t const new_entry_count = s->entry_count + entry_inc;

  if (new_entry_count > s->entry_max)
    {
      do
        {
          s->entry_max *= 2;
        }
      while (new_entry_count > s->entry_max);

      s->entries = (struct svg_stack_entry *)realloc(s->entries,
                                                     sizeof(struct svg_stack_entry) * s->entry_max);
    }

  uint32_t const new_buf_count = s->buf_count + buf_inc;

  if (new_buf_count > s->buf_max)
    {
      do
        {
          s->buf_max *= 2;
        }
      while (new_buf_count > s->buf_max);

      s->buf = realloc(s->buf, s->buf_max);
    }
}

//
//
//

static void
svg_stack_push(struct svg_stack * s, void * v, uint32_t const len)
{
  svg_stack_ensure(s, 1, len);

  s->entries[s->entry_count].idx = s->buf_count;
  s->entries[s->entry_count].len = len;

  memcpy((void *)((uintptr_t)s->buf + s->buf_count), v, len);

  s->entry_count += 1;
  s->buf_count += len;
}

static void
svg_stack_entry_get(struct svg_stack const * s, uint32_t idx, void ** v, uint32_t * len)
{
  struct svg_stack_entry * e = s->entries + idx;

  *v   = (void *)((uintptr_t)s->buf + e->idx);
  *len = e->len;
}

static void
svg_stack_entry_get_range(struct svg_stack const * s,
                          uint32_t                 idx,
                          uint32_t *               pos,
                          uint32_t *               limit)
{
  *pos = *limit = 0u;
  if (idx < s->entry_count)
    {
      struct svg_stack_entry const * const e = s->entries + idx;
      *pos                                   = e->idx;
      *limit                                 = e->idx + e->len;
    }
}

static void *
svg_stack_tos(struct svg_stack const * s)
{
  struct svg_stack_entry * e = s->entries + s->entry_count - 1;

  return (void *)((uintptr_t)s->buf + e->idx);
}

static void
svg_stack_tos_append(struct svg_stack * s, void * v, uint32_t len)
{
  svg_stack_ensure(s, 0, len);

  void * tos = (void *)((uintptr_t)s->buf + s->buf_count);

  memcpy(tos, v, len);

  s->buf_count += len;

  struct svg_stack_entry * e = s->entries + s->entry_count - 1;

  e->len += len;
}

static void
svg_stack_tos_copy(struct svg_stack * to, struct svg_stack const * from)
{
  svg_stack_tos_append(to, from->buf, from->buf_count);
}

static bool
svg_stack_entry_not_equal(struct svg_stack const * s, struct svg_stack const * t, uint32_t idx)
{
  struct svg_stack_entry * s_entry = s->entries + idx;
  struct svg_stack_entry * t_entry = t->entries + idx;

  if (s_entry->len != t_entry->len)
    return true;

  return memcmp((void *)((uintptr_t)s->buf + s_entry->idx),
                (void *)((uintptr_t)t->buf + t_entry->idx),
                s_entry->len) != 0;
}

static bool
svg_stack_equal(struct svg_stack const * s, struct svg_stack const * t)
{
  if ((s->entry_count != t->entry_count) || (s->buf_count != t->buf_count))
    return false;

  return memcmp(s->buf, t->buf, s->buf_count) == 0;
}

static void
svg_stack_drop(struct svg_stack * s)
{
  if (s->entry_count == 0)
    return;

  s->entry_count -= 1;

  struct svg_stack_entry * e = s->entries + s->entry_count;

  s->buf_count -= e->len;
}

static void
svg_stack_pop(struct svg_stack * s, void ** v, uint32_t * len)
{
  if (s->entry_count == 0)
    return;

  s->entry_count -= 1;

  svg_stack_entry_get(s, s->entry_count, v, len);

  s->buf_count -= *len;
}

#ifdef SVG_EXTRA_DEBUGGING

static void
svg_stack_pop_all(struct svg_stack * s, svg_on_stack_drop_pfn on_drop, void * extra)
{
  while (s->entry_count > 0)
    {
      void *   v;
      uint32_t len;

      svg_stack_pop(s, &v, &len);

      on_drop(v, len, extra);
    }
}

#endif

static void
svg_stack_diff(struct svg_stack *    prev,
               struct svg_stack *    curr,
               svg_on_stack_drop_pfn on_drop,
               svg_on_stack_push_pfn on_push,
               void *                extra)
{
  while (prev->entry_count > curr->entry_count)
    {
      void *   v;
      uint32_t len;

      svg_stack_pop(prev, &v, &len);

      on_drop(v, len, extra);
    }

  uint32_t const m        = MIN_MACRO(uint32_t, prev->entry_count, curr->entry_count);
  uint32_t       curr_idx = m;

  for (uint32_t prev_idx = 0; prev_idx < m; prev_idx++)
    {
      if (svg_stack_entry_not_equal(prev, curr, prev_idx))
        {
          curr_idx = prev_idx;  // start work on b from the point of mismatch

          while (prev_idx < prev->entry_count)
            {
              void *   v;
              uint32_t len;

              svg_stack_entry_get(prev, prev_idx, &v, &len);

              on_drop(v, len, extra);

              prev->buf_count -= len;  // adjust end

              prev_idx += 1;
            }

          prev->entry_count = curr_idx;  // adjust count

          break;
        }
    }

  while (curr_idx < curr->entry_count)
    {
      void *   v;
      uint32_t len;

      svg_stack_entry_get(curr, curr_idx, &v, &len);

      svg_stack_push(prev, v, len);

      on_push(v, len, extra);

      curr_idx += 1;
    }
}

//
//
//

struct svg_attribs
{
  //
  // fixed-length state
  //
  union
  {
    struct
    {
      uint32_t elem_count;  // total # of elems

      float opacity;  // global opacity

      svg_paint_op     fill_op;       // fill enabled?
      svg_color_t      fill_color;    // fill rgb
      float            fill_opacity;  // fill opacity
      svg_fill_rule_op fill_rule;     // even-odd or non-zero

      svg_paint_op stroke_op;       // stroke enabled?
      svg_color_t  stroke_color;    // stroke rgb
      float        stroke_opacity;  // stroke opacity
      float        stroke_width;    // stroke width

      svg_paint_op svg_marker_op;  // marker enabled?
      svg_color_t  marker_color;   // stroke rgb
    };

    uint32_t u32[SVG_ATTRIB_TYPE_SCALAR_COUNT];
    float    f32[SVG_ATTRIB_TYPE_SCALAR_COUNT];
  };

  //
  // variable-length state
  //
  struct svg_stack * transforms;
  struct svg_stack * ids;
};

//
//
//

static struct svg_attribs *
svg_attribs_create()
{
  struct svg_attribs * const a = malloc(sizeof(*a));

  a->elem_count = 0;

  a->opacity = 1.0f;

  a->fill_op      = SVG_PAINT_OP_COLOR;
  a->fill_color   = 0x000000;
  a->fill_opacity = 1.0f;
  a->fill_rule    = SVG_FILL_RULE_OP_NONZERO;

  a->stroke_op      = SVG_PAINT_OP_NONE;
  a->stroke_color   = 0x000000;
  a->stroke_opacity = 1.0f;
  a->stroke_width   = 1.0f;

  a->svg_marker_op = SVG_PAINT_OP_NONE;
  a->marker_color  = 0x000000;

  a->transforms = svg_stack_create();  // transform commands and implicit drops
  a->ids        = svg_stack_create();  // id_end + id_begin commands

  return a;
}

static void
svg_attribs_dispose(struct svg_attribs * a)
{
  svg_stack_dispose(a->transforms);
  svg_stack_dispose(a->ids);

  free(a);
}

//
//
//

struct svg_parser
{
  struct svg_stack * p;  // path   dictionary
  struct svg_stack * r;  // raster dictionary
  struct svg_stack * l;  // layer  dictionary

  struct svg_attribs * prev;  // previous   render state
  struct svg_attribs * curr;  // cumulative render state

  struct svg_stack * paths;  // stack of parsed paths

  struct svg_stack * undo;        // commands executed upon element close
  uint32_t           undo_count;  // number of commands in current element

  char *   attr_buf;
  uint32_t attr_max;
  uint32_t attr_count;

  bool is_verbose;
};

//
//
//

static struct svg_parser *
svg_parser_create(bool const is_verbose)
{
  struct svg_parser * sp = malloc(sizeof(*sp));

  sp->p = svg_stack_create();
  sp->r = svg_stack_create();
  sp->l = svg_stack_create();

  sp->prev = svg_attribs_create();
  sp->curr = svg_attribs_create();

  sp->paths = svg_stack_create();

  sp->undo       = svg_stack_create();
  sp->undo_count = 0;

  svg_stack_push(sp->undo, &sp->undo_count, sizeof(sp->undo_count));

  sp->attr_max   = 4096 * 4;
  sp->attr_buf   = malloc(sp->attr_max);
  sp->attr_count = 0;

  sp->is_verbose = is_verbose;

  return sp;
}

static void
svg_parser_dispose(struct svg_parser * sp)
{
  svg_stack_dispose(sp->p);
  svg_stack_dispose(sp->r);
  svg_stack_dispose(sp->l);

  svg_attribs_dispose(sp->prev);
  svg_attribs_dispose(sp->curr);

  svg_stack_dispose(sp->paths);

  svg_stack_dispose(sp->undo);

  free(sp->attr_buf);

  free(sp);
}

//
//
//

struct svg
{
  struct svg_stack * p;  // path   dictionary
  struct svg_stack * r;  // raster dictionary
  struct svg_stack * l;  // layer  dictionary
};

//
// Common iterator struct.
//

// Common struct used by all iterator types.
struct svg_iterator
{
  uintptr_t                     buf;
  uint32_t                      pos;
  uint32_t                      limit;
  struct svg_lookup_cmd const * lookups;
};

static void
svg_iterator_init(struct svg_iterator *               iterator,
                  struct svg_stack const * const      stack,
                  struct svg_lookup_cmd const * const lookups,
                  uint32_t                            path_index)
{
  iterator->buf     = (uintptr_t)stack->buf;
  iterator->pos     = 0;
  iterator->limit   = stack->buf_count;
  iterator->lookups = lookups;
  if (path_index != UINT32_MAX)
    svg_stack_entry_get_range(stack, path_index, &iterator->pos, &iterator->limit);
}

static bool
svg_iterator_next_internal(struct svg_iterator * iterator, void ** ptr)
{
  if (iterator->pos >= iterator->limit)
    return false;

  *ptr = (void *)(iterator->buf + iterator->pos);
  iterator->pos += iterator->lookups[*(unsigned char *)(*ptr)].size;
  return true;
}

//
// Path iterator
//

struct svg_path_iterator
{
  struct svg_iterator base;
};

struct svg_path_iterator *
svg_path_iterator_create(struct svg const * sd, uint32_t path_index)
{
  struct svg_path_iterator * iterator = malloc(sizeof(*iterator));
  svg_iterator_init(&iterator->base, sd->p, path_lookup_cmds, path_index);
  return iterator;
}

bool
svg_path_iterator_next(struct svg_path_iterator * iterator, union svg_path_cmd const ** cmd)
{
  return svg_iterator_next_internal(&iterator->base, (void **)cmd);
}

void
svg_path_iterator_dispose(struct svg_path_iterator * iterator)
{
  free(iterator);
}

//
// Raster iterator
//

struct svg_raster_iterator
{
  struct svg_iterator base;
};

struct svg_raster_iterator *
svg_raster_iterator_create(struct svg const * sd, uint32_t raster_index)
{
  struct svg_raster_iterator * iterator = malloc(sizeof(*iterator));
  svg_iterator_init(&iterator->base, sd->r, raster_lookup_cmds, raster_index);
  return iterator;
}

bool
svg_raster_iterator_next(struct svg_raster_iterator * iterator, union svg_raster_cmd const ** cmd)
{
  return svg_iterator_next_internal(&iterator->base, (void **)cmd);
}

void
svg_raster_iterator_dispose(struct svg_raster_iterator * iterator)
{
  free(iterator);
}

//
// Layer iterator
//

struct svg_layer_iterator
{
  struct svg_iterator base;
};

struct svg_layer_iterator *
svg_layer_iterator_create(struct svg const * sd, uint32_t layer_index)
{
  struct svg_layer_iterator * iterator = malloc(sizeof(*iterator));
  svg_iterator_init(&iterator->base, sd->l, layer_lookup_cmds, layer_index);
  return iterator;
}

bool
svg_layer_iterator_next(struct svg_layer_iterator * iterator, union svg_layer_cmd const ** cmd)
{
  return svg_iterator_next_internal(&iterator->base, (void **)cmd);
}

void
svg_layer_iterator_dispose(struct svg_layer_iterator * iterator)
{
  free(iterator);
}

//
//
//

uint32_t
svg_path_count(struct svg const * const sd)
{
  return svg_stack_entry_count(sd->p);
}

uint32_t
svg_raster_count(struct svg const * const sd)
{
  return svg_stack_entry_count(sd->r);
}

uint32_t
svg_layer_count(struct svg const * const sd)
{
  return svg_stack_entry_count(sd->l);
}

//
//
//

static struct svg *
svg_create(struct svg_parser * sp)
{
  struct svg * const sd = malloc(sizeof(*sd));

  sd->p = sp->p;
  sd->r = sp->r;
  sd->l = sp->l;

  // steal stacks from svg_parser
  sp->p = NULL;
  sp->r = NULL;
  sp->l = NULL;

  return sd;
}

//
//
//

void
svg_dispose(struct svg * const sd)
{
  if (sd != NULL)
    {
      svg_stack_dispose(sd->p);
      svg_stack_dispose(sd->r);
      svg_stack_dispose(sd->l);

      free(sd);
    }
}

//
// Threaded SVG representation
//
// element attributes : id
// container elements : svg, g
// path elements      : circle, ellipse, line, path, polygon, polyline, rect
// raster attributes  : transform, fill|stroke|marker, style props (*)
// layer attributes   : fill-rule, opacities, colors or gradient references, style props (*)
//
//   (*) --> raster and layer attributes also appear in style properties
//
//   1. decode path elements into path dictionary and save reference.
//      either create a sealed path for every element or seal the path
//      only when there is a new name, transform or fill^stroke change,
//      color change or stroke-width.
//
//   2. decode raster attributes into raster dictionary and save
//      reference. transform ops followed by stroked/filled path
//      head. append raster reference to current layer. either create
//      a new raster and a new layer for every path or "compact" and
//      create a new raster when there is a new name, new transform,
//      fill^stroke change, color change or stroke-width change. And
//      for compacted layers, create a new layer whenever there is a
//      fill^stroke change or color change.
//
//      - *begin*
//      - # of rasters before (start index)
//      - # of sub rasters    (can be zero!)
//      - (*transform*)?
//      - (*fill-or-stroke*)
//      - (*path*)
//      - *end*  -> restores stack
//
//   3. add layer attributes into layer dictionary:
//
//      - *begin*
//      - *layer*
//      - *color* | *gradient* | *texture* | ...
//      - fill rule
//      - { *place*, raster, tx, ty }+
//      - *end*
//

//
// UNDO STACK
//

struct svg_attrib_restore
{
  svg_attrib_type type;
  uint32_t        value;
};

static void
svg_attribs_undo(struct svg_parser * sp)
{
  while (sp->undo_count > 0)
    {
      sp->undo_count -= 1;

      void *   v;
      uint32_t len;

      svg_stack_pop(sp->undo, &v, &len);

      struct svg_attrib_restore const * r = (struct svg_attrib_restore *)v;

      if (r->type < SVG_ATTRIB_TYPE_SCALAR_COUNT)
        {
          sp->curr->u32[r->type] = r->value;
        }
      else if (r->type == SVG_ATTRIB_TYPE_TRANSFORM)
        {
          svg_stack_drop(sp->curr->transforms);
        }
    }
}

static void
svg_attribs_save(struct svg_parser * sp, svg_attrib_type t, uint32_t s)
{
  sp->undo_count += 1;

  struct svg_attrib_restore r = { t, s };

  svg_stack_push(sp->undo, &r, sizeof(r));
}

static void
svg_attribs_save_scalar(struct svg_parser * sp, svg_attrib_type t)
{
  assert(t < SVG_ATTRIB_TYPE_SCALAR_COUNT);

  svg_attribs_save(sp, t, sp->curr->u32[t]);
}

static void
svg_attribs_restore_undo_count(struct svg_parser * sp)
{
  void *   v;
  uint32_t len;

  svg_stack_pop(sp->undo, &v, &len);

  sp->undo_count = *(uint32_t *)v;
}

static void
svg_attribs_update(struct svg_parser * sp)
{
  for (uint32_t ii = 0; ii < SVG_ATTRIB_TYPE_SCALAR_COUNT; ii++)
    sp->prev->u32[ii] = sp->curr->u32[ii];
}

static uint32_t
svg_attribs_changes(struct svg_parser * sp)
{
  uint32_t changes = 0;

  for (uint32_t ii = 0; ii < SVG_ATTRIB_TYPE_SCALAR_COUNT; ii++)
    {
      if (sp->prev->u32[ii] != sp->curr->u32[ii])
        changes |= SVG_ATTRIB_TYPE_TO_MASK(ii);
    }

#if 1
  //
  // Option 1: Accumulate non-conflicting non-zero filled paths into a
  // larger path.  This is normally OK but we may want to disable this
  // for SVG correctness because overlapping non-zero filled paths with
  // the same attributes (e.g. fill color) may conflict if they have
  // different winding order.
  //
  // Note that even-odd fill rule paths can't ever be accumulated like
  // non-zero so we always treat these paths as independent.
  //
  if (sp->curr->u32[SVG_ATTRIB_TYPE_FILL_RULE] == SVG_FILL_RULE_OP_EVENODD)
    changes |= SVG_ATTRIB_TYPE_TO_MASK(SVG_ATTRIB_TYPE_FILL_RULE);
#else
  //
  // Option 2: Conservative. Each path is unique.
  //
  changes |= SVG_ATTRIB_TYPE_TO_MASK(SVG_ATTRIB_TYPE_FILL_RULE);
#endif

  // transform change?
  if (!svg_stack_equal(sp->prev->transforms, sp->curr->transforms))
    changes |= 1u << SVG_ATTRIB_TYPE_TRANSFORM;

  //
  // NOTE(allanmac): the parser doesn't actually do anything with IDs
  // right now so an ID change is ignored.
  //
  // if (!svg_stack_equal(sp->prev->ids,sp->curr->ids))
  //   changes |= 1u << SVG_ATTRIB_TYPE_ID;
  //

  return changes;
}

static bool
svg_attribs_changed(uint32_t changes, svg_attrib_type type)
{
  return (changes & SVG_ATTRIB_TYPE_TO_MASK_BIT(type)) != 0;
}

//
// DEFINE CHANGE MASKS
//

#define SVG_CHANGE_MASK_NEW_PATH                                                                   \
  (SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_ELEM_COUNT) |                                       \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_OPACITY) |                                          \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_FILL_OP) |                                          \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_FILL_RULE) |                                        \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_FILL_COLOR) |                                       \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_FILL_OPACITY) |                                     \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_STROKE_OP) |                                        \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_STROKE_COLOR) |                                     \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_STROKE_OPACITY) |                                   \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_STROKE_WIDTH) |                                     \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_SVG_MARKER_OP) |                                    \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_MARKER_COLOR) |                                     \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_TRANSFORM) |                                        \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_ID))

#define SVG_CHANGE_MASK_NEW_RASTER                                                                 \
  (SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_ELEM_COUNT) |                                       \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_OPACITY) |                                          \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_FILL_OP) |                                          \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_FILL_RULE) |                                        \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_FILL_COLOR) |                                       \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_FILL_OPACITY) |                                     \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_STROKE_OP) |                                        \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_STROKE_COLOR) |                                     \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_STROKE_OPACITY) |                                   \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_STROKE_WIDTH) |                                     \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_SVG_MARKER_OP) |                                    \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_MARKER_COLOR) |                                     \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_TRANSFORM) |                                        \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_ID))

#define SVG_CHANGE_MASK_NEW_LAYER                                                                  \
  (SVG_ATTRIB_TYPE_TO_MASK_OFF(SVG_ATTRIB_TYPE_ELEM_COUNT) |                                       \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_OPACITY) |                                          \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_FILL_OP) |                                          \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_FILL_RULE) |                                        \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_FILL_COLOR) |                                       \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_FILL_OPACITY) |                                     \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_STROKE_OP) |                                        \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_STROKE_COLOR) |                                     \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_STROKE_OPACITY) |                                   \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_STROKE_WIDTH) |                                     \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_SVG_MARKER_OP) |                                    \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_MARKER_COLOR) |                                     \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_TRANSFORM) |                                        \
   SVG_ATTRIB_TYPE_TO_MASK_BIT(SVG_ATTRIB_TYPE_ID))

//
//
//

#define SVG_PAINT_OP_BITS_IDX 4
#define SVG_PAINT_OP_MASK_IDX ((1 << SVG_PAINT_OP_BITS_IDX) - 1)

#define SVG_PAINT_OP_BITS_CMD 4
#define SVG_PAINT_OP_MASK_CMD ((1 << SVG_PAINT_OP_BITS_CMD) - 1)

#define SVG_PAINT_OP_BITS_TOTAL (SVG_PAINT_OP_BITS_IDX + SVG_PAINT_OP_BITS_CMD)
#define SVG_PAINT_OP_MASK_TOTAL ((1 << SVG_PAINT_OP_BITS_TOTAL) - 1)

#define SVG_ATTRIB_PAINT_OPS_ANY                                                                   \
  ((((SVG_RASTER_CMD_FILL << SVG_PAINT_OP_BITS_CMD) | SVG_ATTRIB_TYPE_FILL_OP)                     \
    << (0 * SVG_PAINT_OP_BITS_TOTAL)) |                                                            \
   (((SVG_RASTER_CMD_STROKE << SVG_PAINT_OP_BITS_CMD) | SVG_ATTRIB_TYPE_STROKE_OP)                 \
    << (1 * SVG_PAINT_OP_BITS_TOTAL)) |                                                            \
   (((SVG_RASTER_CMD_MARKER << SVG_PAINT_OP_BITS_CMD) | SVG_ATTRIB_TYPE_SVG_MARKER_OP)             \
    << (2 * SVG_PAINT_OP_BITS_TOTAL)))

//
//
//

uint32_t
svg_attrib_paint_op_first_idx(uint32_t const ops)
{
  return ops & SVG_PAINT_OP_MASK_IDX;
}

svg_raster_cmd_type
svg_attrib_paint_op_first_cmd(uint32_t const ops)
{
  return (svg_raster_cmd_type)((ops >> SVG_PAINT_OP_BITS_IDX) & SVG_PAINT_OP_MASK_CMD);
}

uint32_t
svg_attrib_paint_op_drop(uint32_t const ops)
{
  return ops >> SVG_PAINT_OP_BITS_TOTAL;
}

uint32_t
paint_enabled_first(struct svg_parser * sp, uint32_t ops)
{
  while (ops != 0)
    {
      if (sp->curr->u32[svg_attrib_paint_op_first_idx(ops)] == SVG_PAINT_OP_COLOR)
        return ops;

      ops = svg_attrib_paint_op_drop(ops);
    }

  return ops;
}

static bool
paint_enabled_any(struct svg_attribs const * const a, uint32_t ops)
{
  while (ops != 0)
    {
      if (a->u32[svg_attrib_paint_op_first_idx(ops)] == SVG_PAINT_OP_COLOR)
        return true;

      ops = svg_attrib_paint_op_drop(ops);
    }

  return false;
}

static bool
paint_was_enabled(struct svg_parser * sp)
{
  return paint_enabled_any(sp->prev, SVG_ATTRIB_PAINT_OPS_ANY);
}

static bool
paint_is_enabled(struct svg_parser * sp)
{
  return paint_enabled_any(sp->curr, SVG_ATTRIB_PAINT_OPS_ANY);
}

//
//
//

static void
compile_end(struct svg_parser * sp)
{
  //
  // ALWAYS END THE CURRENT PATH CLAUSE
  //
  uint32_t const p = svg_stack_entry_count(sp->p);

  if (p > 0)
    {
      struct svg_path_cmd_end pce = { SVG_PATH_CMD_END, p - 1 };
      svg_stack_tos_append(sp->p, &pce, sizeof(pce));
    }

  //
  // IF THERE WAS A PAINT IN PROGRESS THEN:
  // - END THE RASTER
  // - PLACE THE RASTER ON THE WIP LAYER
  // - END THE LAYER
  //
  uint32_t const r_idx    = svg_stack_entry_count(sp->r);
  bool const     first_rl = r_idx == 0;

  if (!first_rl && paint_was_enabled(sp))
    {
      struct svg_raster_cmd_end rce = { SVG_RASTER_CMD_END, r_idx - 1 };
      svg_stack_tos_append(sp->r, &rce, sizeof(rce));

      struct svg_layer_cmd_place lcp = { SVG_LAYER_CMD_PLACE, r_idx - 1, 0, 0 };
      svg_stack_tos_append(sp->l, &lcp, sizeof(lcp));

      struct svg_layer_cmd_end lce = { SVG_LAYER_CMD_END };
      svg_stack_tos_append(sp->l, &lce, sizeof(lce));
    }
}

//
//
//

static void
svg_raster_add_path(struct svg_parser * sp, uint32_t ops)
{
  if ((ops = paint_enabled_first(sp, ops)) == 0)
    return;

  while (true)
    {
      struct svg_raster_cmd_fsm fsm = { svg_attrib_paint_op_first_cmd(ops),
                                        svg_stack_entry_count(sp->p) - 1 };

      svg_stack_tos_append(sp->r, &fsm, sizeof(fsm));

      ops = svg_attrib_paint_op_drop(ops);

      if ((ops = paint_enabled_first(sp, ops)) == 0)
        return;

      //
      // otherwise, end this raster and start another
      //
      uint32_t const rid = svg_stack_entry_count(sp->r) - 1;

      struct svg_raster_cmd_end rce = { SVG_RASTER_CMD_END, rid };
      svg_stack_tos_append(sp->r, &rce, sizeof(rce));

      struct svg_layer_cmd_place lcp = { SVG_LAYER_CMD_PLACE, rid, 0, 0 };
      svg_stack_tos_append(sp->l, &lcp, sizeof(lcp));

      struct svg_layer_cmd_end lce = { SVG_LAYER_CMD_END };
      svg_stack_tos_append(sp->l, &lce, sizeof(lce));

      struct svg_raster_cmd_begin rcb = { SVG_RASTER_CMD_BEGIN };
      svg_stack_push(sp->r, &rcb, sizeof(rcb));

      struct svg_layer_cmd_begin lcb = { SVG_LAYER_CMD_BEGIN, svg_stack_entry_count(sp->l) };
      svg_stack_push(sp->l, &lcb, sizeof(lcb));
    }
}

//
//
//

static void
transform_on_drop(void * v, uint32_t len, void * extra)
{
  struct svg_parser *                  sp  = extra;
  struct svg_raster_cmd_transform_drop cmd = { SVG_RASTER_CMD_TRANSFORM_DROP };

  svg_stack_tos_append(sp->r, &cmd, sizeof(cmd));
}

static void
transform_on_push(void * v, uint32_t len, void * extra)
{
  struct svg_parser * sp = extra;

  svg_stack_tos_append(sp->r, v, len);
}

//
// PROCESS PATH ELEMENTS
//
// Path elements trigger compilation of paths, rasters and layers.
//

static void
compile(struct svg_parser * sp)
{
  //
  // if there are no paths then return and continue to record attrib
  // changes
  //
  bool const paths_empty = svg_stack_entry_count(sp->paths) == 0;

  if (paths_empty)
    return;

  //
  // compute changes
  //
  uint32_t const changes = svg_attribs_changes(sp);

  //
  // compile paths
  //
  // FIXME -- FORCE IF PATHS=0 / EMPTY
  //
  uint32_t const pc           = svg_stack_entry_count(sp->p);
  bool const     pc_is_empty  = pc == 0;
  bool const     path_changed = pc_is_empty || ((changes & SVG_CHANGE_MASK_NEW_PATH) != 0);

  if (path_changed)
    {
      if (pc > 0)
        {
          struct svg_path_cmd_end pce = { SVG_PATH_CMD_END, pc - 1 };
          svg_stack_tos_append(sp->p, &pce, sizeof(pce));
        }

      struct svg_path_cmd_begin pcb = { SVG_PATH_CMD_BEGIN };
      svg_stack_push(sp->p, &pcb, sizeof(pcb));
    }

  // append path commands
  svg_stack_tos_copy(sp->p, sp->paths);

  // reset the path stack
  svg_stack_reset(sp->paths);

  // return if path is unchanged
  if (!path_changed)
    return;

  //
  // 0. if path unchanged then return
  //
  // 1. if !first_rl && raster changed && any fill/stroke/marker *was* enabled
  //          - RASTER_END
  //          - if layer changed then LAYER_END
  //
  // 2. if any fill/stroke/marker paint *is* enabled
  //      - if first_rl || raster changed
  //          - RASTER_BEGIN
  //      - if first_rl || layer changed
  //          - LAYER_BEGIN
  //      - compile transform and stroke-width raster changes
  //      - compile opacity/color/fill-rule layer changes
  //
  // 3. if fill *is* enabled
  //      - add filled path
  //      - if stroke or marker enabled
  //          - RASTER_END
  //          - add RASTER to layer
  //          - LAYER_END
  //          - RASTER_BEGIN
  //          - LAYER_BEGIN
  // 4. if stroke *is* enabled
  //      - add stroked path
  //      - if marker enabled
  //          - RASTER_END
  //          - add RASTER to layer
  //          - LAYER_END
  //          - RASTER_BEGIN
  //          - LAYER_BEGIN
  // 5. if marker *is* enabled
  //      - add marker path
  //

  uint32_t const r_idx          = svg_stack_entry_count(sp->r);
  bool const     first_rl       = r_idx == 0;
  bool const     raster_changed = (changes & SVG_CHANGE_MASK_NEW_RASTER) != 0;
  bool const     layer_changed  = (changes & SVG_CHANGE_MASK_NEW_LAYER) != 0;

  // RASTER WAS ENABLED
  if (!first_rl && raster_changed && paint_was_enabled(sp))
    {
      struct svg_raster_cmd_end rce = { SVG_RASTER_CMD_END, r_idx - 1 };
      svg_stack_tos_append(sp->r, &rce, sizeof(rce));

      struct svg_layer_cmd_place lcp = { SVG_LAYER_CMD_PLACE, r_idx - 1, 0, 0 };
      svg_stack_tos_append(sp->l, &lcp, sizeof(lcp));

      if (layer_changed)
        {
          struct svg_layer_cmd_end lce = { SVG_LAYER_CMD_END };
          svg_stack_tos_append(sp->l, &lce, sizeof(lce));
        }
    }

  if (paint_is_enabled(sp))
    {
      if (first_rl || raster_changed)
        {
          struct svg_raster_cmd_begin rcb = { SVG_RASTER_CMD_BEGIN };
          svg_stack_push(sp->r, &rcb, sizeof(rcb));
        }

      if (first_rl || layer_changed)
        {
          struct svg_layer_cmd_begin lcb = { SVG_LAYER_CMD_BEGIN, svg_stack_entry_count(sp->l) };
          svg_stack_push(sp->l, &lcb, sizeof(lcb));
        }

      //
      // IT SHOULD BE OK TO FRONTLOAD ALL THESE CHANGES SINCE IN THE
      // WORST CASE THEY'LL BE BRACKETED BY ID NAMES
      //
      // compile raster changes... transforms always first
      //
      if (svg_attribs_changed(changes, SVG_ATTRIB_TYPE_TRANSFORM))
        {
          svg_stack_diff(sp->prev->transforms,
                         sp->curr->transforms,
                         transform_on_drop,
                         transform_on_push,
                         sp);
        }

      if (svg_attribs_changed(changes, SVG_ATTRIB_TYPE_STROKE_WIDTH))
        {
          ;  //
        }

      if (layer_changed)  // not checking r_is_empty here (?)
        {
          // compile layer changes: opacity, color, fill-rule changes

          if (svg_attribs_changed(changes, SVG_ATTRIB_TYPE_OPACITY))
            {
              struct svg_layer_cmd_opacity cmd = { SVG_LAYER_CMD_OPACITY, sp->curr->opacity };
              svg_stack_tos_append(sp->l, &cmd, sizeof(cmd));
            }

          if (svg_attribs_changed(changes, SVG_ATTRIB_TYPE_FILL_RULE))
            {
              struct svg_layer_cmd_fill_rule cmd = { SVG_LAYER_CMD_FILL_RULE, sp->curr->fill_rule };
              svg_stack_tos_append(sp->l, &cmd, sizeof(cmd));
            }

          if (svg_attribs_changed(changes, SVG_ATTRIB_TYPE_FILL_COLOR))
            {
              struct svg_layer_cmd_fill_color cmd = { SVG_LAYER_CMD_FILL_COLOR,
                                                      sp->curr->fill_color };
              svg_stack_tos_append(sp->l, &cmd, sizeof(cmd));
            }

          if (svg_attribs_changed(changes, SVG_ATTRIB_TYPE_FILL_OPACITY))
            {
              struct svg_layer_cmd_fill_opacity cmd = { SVG_LAYER_CMD_FILL_OPACITY,
                                                        sp->curr->fill_opacity };
              svg_stack_tos_append(sp->l, &cmd, sizeof(cmd));
            }

          if (svg_attribs_changed(changes, SVG_ATTRIB_TYPE_STROKE_COLOR))
            {
              struct svg_layer_cmd_stroke_color cmd = { SVG_LAYER_CMD_STROKE_COLOR,
                                                        sp->curr->stroke_color };
              svg_stack_tos_append(sp->l, &cmd, sizeof(cmd));
            }

          if (svg_attribs_changed(changes, SVG_ATTRIB_TYPE_STROKE_OPACITY))
            {
              struct svg_layer_cmd_stroke_opacity cmd = { SVG_LAYER_CMD_STROKE_OPACITY,
                                                          sp->curr->stroke_opacity };
              svg_stack_tos_append(sp->l, &cmd, sizeof(cmd));
            }
        }
    }

  //
  // append path and/or create new rasters and layers
  //
  svg_raster_add_path(sp, SVG_ATTRIB_PAINT_OPS_ANY);

  //
  // copy curr attribs to prev attribs
  //
  svg_attribs_update(sp);
}

//
//
//

static void
svg_warning(struct svg_parser * sp, yxml_t * ys, char const * condition, char const * name)
{
  if (sp->is_verbose)
    {
      fprintf(stderr,
              "Warning: %s at line %u column %lu g--> \"%s\"\n",
              condition,
              ys->line,
              ys->byte,
              name);
    }
}

static void
svg_attrib_ignore(struct svg_parser * sp, yxml_t * ys, char const * name)
{
  svg_warning(sp, ys, "ignoring attribute", name);
}

//
// PARSE CONTAINERS
//

static void
svg_parse_elem_svg(struct svg_parser * sp, yxml_t * ys)
{
  ;
}

static void
svg_parse_elem_g(struct svg_parser * sp, yxml_t * ys)
{
  ;
}

//
// PARSE SHAPES
//

static void
svg_parse_elem_circle(struct svg_parser * sp, yxml_t * ys)
{
  struct svg_path_cmd_circle cmd;

  cmd.type = SVG_PATH_CMD_CIRCLE;
  cmd.cx   = 0.0f;
  cmd.cy   = 0.0f;
  cmd.r    = 0.0f;

  svg_stack_push(sp->paths, &cmd, sizeof(cmd));
}

static void
svg_parse_elem_ellipse(struct svg_parser * sp, yxml_t * ys)
{
  struct svg_path_cmd_ellipse cmd;

  cmd.type = SVG_PATH_CMD_ELLIPSE;
  cmd.cx   = 0.0f;
  cmd.cy   = 0.0f;
  cmd.rx   = 0.0f;
  cmd.ry   = 0.0f;

  svg_stack_push(sp->paths, &cmd, sizeof(cmd));
}

static void
svg_parse_elem_line(struct svg_parser * sp, yxml_t * ys)
{
  struct svg_path_cmd_line cmd;

  cmd.type = SVG_PATH_CMD_LINE;
  cmd.x1   = 0.0f;
  cmd.y1   = 0.0f;
  cmd.x2   = 0.0f;
  cmd.y2   = 0.0f;

  svg_stack_push(sp->paths, &cmd, sizeof(cmd));
}

static void
svg_parse_elem_path(struct svg_parser * sp, yxml_t * ys)
{
  struct svg_path_cmd_path_begin cmd;

  cmd.type = SVG_PATH_CMD_PATH_BEGIN;

  svg_stack_push(sp->paths, &cmd, sizeof(cmd));
}

static void
svg_parse_elem_polygon(struct svg_parser * sp, yxml_t * ys)
{
  struct svg_path_cmd_polygon cmd = { SVG_PATH_CMD_POLYGON };

  svg_stack_push(sp->paths, &cmd, sizeof(cmd));
}

static void
svg_parse_elem_polyline(struct svg_parser * sp, yxml_t * ys)
{
  struct svg_path_cmd_polyline cmd = { SVG_PATH_CMD_POLYLINE };

  svg_stack_push(sp->paths, &cmd, sizeof(cmd));
}

static void
svg_parse_elem_rect(struct svg_parser * sp, yxml_t * ys)
{
  struct svg_path_cmd_rect cmd;

  cmd.type   = SVG_PATH_CMD_RECT;
  cmd.x      = 0.0f;
  cmd.y      = 0.0f;
  cmd.width  = 0.0f;
  cmd.height = 0.0f;
  cmd.rx     = 0.0f;
  cmd.ry     = 0.0f;

  svg_stack_push(sp->paths, &cmd, sizeof(cmd));
}

//
// MODIFY OR APPEND TO PATH ELEMENT
//

static bool
svg_paths_empty(struct svg_parser * sp)
{
  return svg_stack_entry_count(sp->paths) == 0;
}

static union svg_path_cmd *
svg_paths_tos(struct svg_parser * sp)
{
  return (union svg_path_cmd *)svg_stack_tos(sp->paths);
}

static void
svg_paths_tos_append(struct svg_parser * sp, void * v, uint32_t len)
{
  svg_stack_tos_append(sp->paths, v, len);
}

//
//
//

static jmp_buf svg_exception;

//
//
//

static void
svg_invalid_attrib(struct svg_parser * sp, yxml_t * ys, char const * val)
{
  if (sp->is_verbose)
    {
      fprintf(stderr, "Error: %u:%lu --> invalid attribute: \"%s\"\n", ys->line, ys->byte, val);
    }

  longjmp(svg_exception, -1024);
}

//
//
//

static float
svg_parse_number(struct svg_parser * sp, yxml_t * ys, char * val)
{
  char * stop = val;

  float f = strtof(val, &stop);

  if (stop == val)
    svg_invalid_attrib(sp, ys, val);

  return f;
}

static uint32_t
svg_parse_numbers(struct svg_parser * sp,
                  yxml_t *            ys,
                  char *              val,
                  uint32_t            len,
                  float *             array,
                  uint32_t            array_count,
                  uint32_t *          parse_count)
{
  *parse_count = 0;

  char * next = val;

  while (*next != 0)
    {
      char * stop = next;

      *array = strtof(next, &stop);

      if (stop == next)
        break;

      next = stop;

      *parse_count += 1;

      // eat trailing whitespace... but let calling routine handle
      // inter-sequence commas

      while (isspace(*next))
        next += 1;

      if (*next == ',')  // eat up to one comma
        next += 1;

      if (*parse_count == array_count)
        break;

      array += 1;
    }

  return (uint32_t)(next - val);
}

//
// PARSE ATTRIBUTES
//

static void
svg_parse_attrib_id(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  svg_stack_push(sp->curr->ids, val, len + 1);  // push the symbol name
}

static void
svg_parse_attrib_r(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  union svg_path_cmd * cmd = svg_paths_tos(sp);

  if (cmd->type != SVG_PATH_CMD_CIRCLE)
    svg_invalid_attrib(sp, ys, val);

  cmd->circle.r = svg_parse_number(sp, ys, val);
}

static void
svg_parse_attrib_cx(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  union svg_path_cmd * cmd = svg_paths_tos(sp);

  if (cmd->type == SVG_PATH_CMD_CIRCLE)
    {
      cmd->circle.cx = svg_parse_number(sp, ys, val);
    }
  else if (cmd->type == SVG_PATH_CMD_ELLIPSE)
    {
      cmd->ellipse.cx = svg_parse_number(sp, ys, val);
    }
  else
    {
      svg_invalid_attrib(sp, ys, val);
    }
}

static void
svg_parse_attrib_cy(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  union svg_path_cmd * cmd = svg_paths_tos(sp);

  if (cmd->type == SVG_PATH_CMD_CIRCLE)
    {
      cmd->circle.cy = svg_parse_number(sp, ys, val);
    }
  else if (cmd->type == SVG_PATH_CMD_ELLIPSE)
    {
      cmd->ellipse.cy = svg_parse_number(sp, ys, val);
    }
  else
    {
      svg_invalid_attrib(sp, ys, val);
    }
}

static void
svg_parse_attrib_rx(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  union svg_path_cmd * cmd = svg_paths_tos(sp);

  if (cmd->type == SVG_PATH_CMD_ELLIPSE)
    {
      cmd->ellipse.rx = svg_parse_number(sp, ys, val);
    }
  else if (cmd->type == SVG_PATH_CMD_RECT)
    {
      cmd->rect.rx = svg_parse_number(sp, ys, val);
    }
  else
    {
      svg_invalid_attrib(sp, ys, val);
    }
}

static void
svg_parse_attrib_ry(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  union svg_path_cmd * cmd = svg_paths_tos(sp);

  if (cmd->type == SVG_PATH_CMD_ELLIPSE)
    {
      cmd->ellipse.ry = svg_parse_number(sp, ys, val);
    }
  else if (cmd->type == SVG_PATH_CMD_RECT)
    {
      cmd->rect.ry = svg_parse_number(sp, ys, val);
    }
  else
    {
      svg_invalid_attrib(sp, ys, val);
    }
}

static void
svg_parse_attrib_x(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  if (svg_paths_empty(sp))
    {
      svg_attrib_ignore(sp, ys, "x");
      return;
    }

  union svg_path_cmd * cmd = svg_paths_tos(sp);

  if (cmd->type != SVG_PATH_CMD_RECT)
    svg_invalid_attrib(sp, ys, "x");

  cmd->rect.x = svg_parse_number(sp, ys, val);
}

static void
svg_parse_attrib_y(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  if (svg_paths_empty(sp))
    {
      svg_attrib_ignore(sp, ys, "y");
      return;
    }

  union svg_path_cmd * cmd = svg_paths_tos(sp);

  if (cmd->type != SVG_PATH_CMD_RECT)
    svg_invalid_attrib(sp, ys, "y");

  cmd->rect.y = svg_parse_number(sp, ys, val);
}

static void
svg_parse_attrib_width(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  if (svg_paths_empty(sp))
    {
      svg_attrib_ignore(sp, ys, "width");
      return;
    }

  union svg_path_cmd * cmd = svg_paths_tos(sp);

  if (cmd->type != SVG_PATH_CMD_RECT)
    {
      // svg_invalid_attrib(sp,ys,val);
      svg_attrib_ignore(sp, ys, "width");
    }

  cmd->rect.width = svg_parse_number(sp, ys, val);
}

static void
svg_parse_attrib_height(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  if (svg_paths_empty(sp))
    {
      svg_attrib_ignore(sp, ys, "height");
      return;
    }

  union svg_path_cmd * cmd = svg_paths_tos(sp);

  if (cmd->type != SVG_PATH_CMD_RECT)
    {
      // svg_invalid_attrib(sp,ys,val);
      svg_attrib_ignore(sp, ys, "height");
    }

  cmd->rect.height = svg_parse_number(sp, ys, val);
}

static void
svg_parse_attrib_x1(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  union svg_path_cmd * cmd = svg_paths_tos(sp);

  if (cmd->type != SVG_PATH_CMD_LINE)
    svg_invalid_attrib(sp, ys, val);

  cmd->line.x1 = svg_parse_number(sp, ys, val);
}

static void
svg_parse_attrib_y1(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  union svg_path_cmd * cmd = svg_paths_tos(sp);

  if (cmd->type != SVG_PATH_CMD_LINE)
    svg_invalid_attrib(sp, ys, val);

  cmd->line.y1 = svg_parse_number(sp, ys, val);
}

static void
svg_parse_attrib_x2(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  union svg_path_cmd * cmd = svg_paths_tos(sp);

  if (cmd->type != SVG_PATH_CMD_LINE)
    svg_invalid_attrib(sp, ys, val);

  cmd->line.x2 = svg_parse_number(sp, ys, val);
}

static void
svg_parse_attrib_y2(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  union svg_path_cmd * cmd = svg_paths_tos(sp);

  if (cmd->type != SVG_PATH_CMD_LINE)
    svg_invalid_attrib(sp, ys, val);

  cmd->line.y2 = svg_parse_number(sp, ys, val);
}

//
//
//

static void
svg_parse_points(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  {
    struct svg_path_cmd_poly_point cmd = { .type = SVG_PATH_CMD_POLY_POINT };

    bool first = true;

    do
      {
        uint32_t parse_count;

        uint32_t n = svg_parse_numbers(sp, ys, val, len, &cmd.x, 2, &parse_count);

        if (parse_count != 2)
          svg_invalid_attrib(sp, ys, val);

        svg_paths_tos_append(sp, &cmd, sizeof(cmd));

        val += n;
        len -= n;

        first = false;
      }
    while (len > 0);
  }

  {
    struct svg_path_cmd_poly_end cmd = { SVG_PATH_CMD_POLY_END };

    svg_paths_tos_append(sp, &cmd, sizeof(cmd));
  }
}

//
//
//

static void
svg_parse_attrib_points(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  // there must be a polyline or polygon
  if (svg_paths_empty(sp))
    {
      svg_invalid_attrib(sp, ys, "points");
    }

  union svg_path_cmd * cmd = svg_paths_tos(sp);

  if ((cmd->type != SVG_PATH_CMD_POLYGON) && (cmd->type != SVG_PATH_CMD_POLYLINE))
    svg_invalid_attrib(sp, ys, val);

  svg_parse_points(sp, ys, val, len);
}

//
//
//

static int
svg_parse_path_coord_sequence(struct svg_parser * sp,
                              yxml_t *            ys,
                              char *              val,
                              uint32_t            len,
                              void *              cmd,
                              uint32_t            cmd_size,
                              float *             cmd_coords,
                              uint32_t const      cmd_coord_count,
                              bool const          optional)
{
  int  t     = 0;
  bool first = true;

  do
    {
      uint32_t parse_count;

      uint32_t n = svg_parse_numbers(sp, ys, val, len, cmd_coords, cmd_coord_count, &parse_count);

      if ((parse_count == 0) && (!first || optional))
        break;

      if (parse_count != cmd_coord_count)
        svg_invalid_attrib(sp, ys, val);

      svg_stack_push(sp->paths, cmd, cmd_size);

      first = false;
      t += n;

      val += n;
      len -= n;
    }
  while (len > 0);

  return t;
}

//
//
//

static int
svg_parse_path_move_to(struct svg_parser *     sp,
                       yxml_t *                ys,
                       char *                  val,
                       uint32_t                len,
                       bool const              first_cmd,
                       svg_path_cmd_type const type)
{
  struct svg_path_cmd_move_to cmd = { .type = type };

  uint32_t parse_count;
  uint32_t n = svg_parse_numbers(sp, ys, val, len, &cmd.x, 2, &parse_count);

  if (parse_count != 2)
    svg_invalid_attrib(sp, ys, val);

  svg_stack_push(sp->paths, &cmd, sizeof(cmd));

  return n;
}

static int
svg_parse_path_close(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  struct svg_path_cmd_close cmd = { .type = SVG_PATH_CMD_CLOSE };

  svg_stack_push(sp->paths, &cmd, sizeof(cmd));

  return 0;
}

static int
svg_parse_path_line_to(struct svg_parser *     sp,
                       yxml_t *                ys,
                       char *                  val,
                       uint32_t                len,
                       svg_path_cmd_type const type,
                       const bool              optional)
{
  struct svg_path_cmd_line_to cmd = { .type = type };

  return svg_parse_path_coord_sequence(sp, ys, val, len, &cmd, sizeof(cmd), &cmd.x, 2, optional);
}

static int
svg_parse_path_hv_line_to(
  struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len, svg_path_cmd_type const type)
{
  struct svg_path_cmd_coord_to cmd = { .type = type };

  return svg_parse_path_coord_sequence(sp, ys, val, len, &cmd, sizeof(cmd), &cmd.c, 1, false);
}

static int
svg_parse_path_cubic_to(
  struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len, svg_path_cmd_type const type)
{
  struct svg_path_cmd_cubic_to cmd = { .type = type };

  return svg_parse_path_coord_sequence(sp, ys, val, len, &cmd, sizeof(cmd), &cmd.x1, 6, false);
}

static int
svg_parse_path_cubic_smooth_to(
  struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len, svg_path_cmd_type const type)
{
  struct svg_path_cmd_cubic_smooth_to cmd = { .type = type };

  return svg_parse_path_coord_sequence(sp, ys, val, len, &cmd, sizeof(cmd), &cmd.x2, 4, false);
}

static int
svg_parse_path_quad_to(
  struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len, svg_path_cmd_type const type)
{
  struct svg_path_cmd_quad_to cmd = { .type = type };

  return svg_parse_path_coord_sequence(sp, ys, val, len, &cmd, sizeof(cmd), &cmd.x1, 4, false);
}

static int
svg_parse_path_quad_smooth_to(
  struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len, svg_path_cmd_type const type)
{
  struct svg_path_cmd_quad_smooth_to cmd = { .type = type };

  return svg_parse_path_coord_sequence(sp, ys, val, len, &cmd, sizeof(cmd), &cmd.x, 2, false);
}

static int
svg_parse_path_rat_cubic_to(
  struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len, svg_path_cmd_type const type)
{
  struct svg_path_cmd_rat_cubic_to cmd = { .type = type };

  return svg_parse_path_coord_sequence(sp, ys, val, len, &cmd, sizeof(cmd), &cmd.x1, 8, false);
}

static int
svg_parse_path_rat_quad_to(
  struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len, svg_path_cmd_type const type)
{
  struct svg_path_cmd_rat_quad_to cmd = { .type = type };

  return svg_parse_path_coord_sequence(sp, ys, val, len, &cmd, sizeof(cmd), &cmd.x1, 5, false);
}

static int
svg_parse_path_arc_to(
  struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len, svg_path_cmd_type const type)
{
  struct svg_path_cmd_arc_to cmd = { .type = type };

  return svg_parse_path_coord_sequence(sp, ys, val, len, &cmd, sizeof(cmd), &cmd.rx, 7, false);
}

//
//
//

static void
svg_parse_attrib_d(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  if (svg_paths_tos(sp)->type != SVG_PATH_CMD_PATH_BEGIN)
    svg_invalid_attrib(sp, ys, val);

  bool first_cmd = true;

  do
    {
      int  n = len;
      char t[2];

      int const err = sscanf(val, " %1[ACDHLMQRSTVZacdhlmqrstvz]%n", t, &n);

      if (err != 1)
        break;

      val += n;
      len -= n;

      // fprintf(stderr,"%s\n",t);

      switch (t[0])
        {
            //
            // ABSOLUTE
            //
          case 'A':
            n = svg_parse_path_arc_to(sp, ys, val, len, SVG_PATH_CMD_ARC_TO);
            break;

          case 'C':
            n = svg_parse_path_cubic_to(sp, ys, val, len, SVG_PATH_CMD_CUBIC_TO);
            break;

          case 'D':
            n = svg_parse_path_rat_cubic_to(sp, ys, val, len, SVG_PATH_CMD_RAT_CUBIC_TO);
            break;

          case 'H':
            n = svg_parse_path_hv_line_to(sp, ys, val, len, SVG_PATH_CMD_HLINE_TO);
            break;

          case 'L':
            n = svg_parse_path_line_to(sp, ys, val, len, SVG_PATH_CMD_LINE_TO, false);
            break;

          case 'M':
            // parse move to and optional line to's
            n = svg_parse_path_move_to(sp, ys, val, len, first_cmd, SVG_PATH_CMD_MOVE_TO);
            val += n;
            len -= n;
            n = svg_parse_path_line_to(sp, ys, val, len, SVG_PATH_CMD_LINE_TO, true);
            // reset first
            first_cmd = false;
            break;

          case 'Q':
            n = svg_parse_path_quad_to(sp, ys, val, len, SVG_PATH_CMD_QUAD_TO);
            break;

          case 'R':
            n = svg_parse_path_rat_quad_to(sp, ys, val, len, SVG_PATH_CMD_RAT_QUAD_TO);
            break;

          case 'S':
            n = svg_parse_path_cubic_smooth_to(sp, ys, val, len, SVG_PATH_CMD_CUBIC_SMOOTH_TO);
            break;

          case 'T':
            n = svg_parse_path_quad_smooth_to(sp, ys, val, len, SVG_PATH_CMD_QUAD_SMOOTH_TO);
            break;

          case 'V':
            n = svg_parse_path_hv_line_to(sp, ys, val, len, SVG_PATH_CMD_VLINE_TO);
            break;

          case 'Z':
            n = svg_parse_path_close(sp, ys, val, len);
            break;

            //
            // RELATIVE
            //
          case 'a':
            n = svg_parse_path_arc_to(sp, ys, val, len, SVG_PATH_CMD_ARC_TO_REL);
            break;

          case 'c':
            n = svg_parse_path_cubic_to(sp, ys, val, len, SVG_PATH_CMD_CUBIC_TO_REL);
            break;

          case 'd':
            n = svg_parse_path_rat_cubic_to(sp, ys, val, len, SVG_PATH_CMD_RAT_CUBIC_TO_REL);
            break;

          case 'h':
            n = svg_parse_path_hv_line_to(sp, ys, val, len, SVG_PATH_CMD_HLINE_TO_REL);
            break;

          case 'l':
            n = svg_parse_path_line_to(sp, ys, val, len, SVG_PATH_CMD_LINE_TO_REL, false);
            break;

          case 'm':
            // if relative move_to is first command in path then force to absolute
            n = svg_parse_path_move_to(sp,
                                       ys,
                                       val,
                                       len,
                                       first_cmd,
                                       first_cmd ? SVG_PATH_CMD_MOVE_TO : SVG_PATH_CMD_MOVE_TO_REL);
            val += n;
            len -= n;
            n = svg_parse_path_line_to(sp, ys, val, len, SVG_PATH_CMD_LINE_TO_REL, true);
            // reset first
            first_cmd = false;
            break;

          case 'q':
            n = svg_parse_path_quad_to(sp, ys, val, len, SVG_PATH_CMD_QUAD_TO_REL);
            break;

          case 'r':
            n = svg_parse_path_rat_quad_to(sp, ys, val, len, SVG_PATH_CMD_RAT_QUAD_TO_REL);
            break;

          case 's':
            n = svg_parse_path_cubic_smooth_to(sp, ys, val, len, SVG_PATH_CMD_CUBIC_SMOOTH_TO_REL);
            break;

          case 't':
            n = svg_parse_path_quad_smooth_to(sp, ys, val, len, SVG_PATH_CMD_QUAD_SMOOTH_TO_REL);
            break;

          case 'v':
            n = svg_parse_path_hv_line_to(sp, ys, val, len, SVG_PATH_CMD_VLINE_TO_REL);
            break;

          case 'z':
            n = svg_parse_path_close(sp, ys, val, len);
            break;

          default:
            svg_invalid_attrib(sp, ys, t);
        }

      val += n;
      len -= n;
    }
  while (len > 0);

  //
  //
  //

  struct svg_path_cmd_path_end cmd = { .type = SVG_PATH_CMD_PATH_END };

  svg_stack_push(sp->paths, &cmd, sizeof(cmd));
}

//
// PARSE RENDER STATE ATTRIBS -- VARIABLE LENGTH
//

static void
svg_parse_transform_project(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  struct svg_raster_cmd_transform_project cmd = { .type = SVG_RASTER_CMD_TRANSFORM_PROJECT };

  uint32_t parse_count;

  svg_parse_numbers(sp, ys, val, len, &cmd.sx, 8, &parse_count);

  if (parse_count != 8)
    svg_invalid_attrib(sp, ys, val);

  svg_stack_push(sp->curr->transforms, &cmd, sizeof(cmd));
}

static void
svg_parse_transform_matrix(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  struct svg_raster_cmd_transform_matrix cmd = { .type = SVG_RASTER_CMD_TRANSFORM_MATRIX };

  uint32_t parse_count;

  svg_parse_numbers(sp, ys, val, len, &cmd.sx, 6, &parse_count);

  if (parse_count != 6)
    svg_invalid_attrib(sp, ys, val);

  svg_stack_push(sp->curr->transforms, &cmd, sizeof(cmd));
}

static void
svg_parse_transform_translate(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  struct svg_raster_cmd_transform_translate cmd = { .type = SVG_RASTER_CMD_TRANSFORM_TRANSLATE,
                                                    0.0f,
                                                    0.0f };

  uint32_t parse_count;

  svg_parse_numbers(sp, ys, val, len, &cmd.tx, 2, &parse_count);

  if (parse_count < 1)
    svg_invalid_attrib(sp, ys, val);

  svg_stack_push(sp->curr->transforms, &cmd, sizeof(cmd));
}

static void
svg_parse_transform_scale(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  struct svg_raster_cmd_transform_scale cmd = { .type = SVG_RASTER_CMD_TRANSFORM_SCALE };

  uint32_t parse_count;

  svg_parse_numbers(sp, ys, val, len, &cmd.sx, 2, &parse_count);

  if (parse_count < 1)
    svg_invalid_attrib(sp, ys, val);

  if (parse_count == 1)
    cmd.sy = cmd.sx;

  svg_stack_push(sp->curr->transforms, &cmd, sizeof(cmd));
}

static void
svg_parse_transform_rotate(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  struct svg_raster_cmd_transform_rotate cmd = { .type = SVG_RASTER_CMD_TRANSFORM_ROTATE,
                                                 0.0f,
                                                 0.0f,
                                                 0.0f };

  uint32_t parse_count;

  svg_parse_numbers(sp, ys, val, len, &cmd.d, 3, &parse_count);

  if (parse_count < 1)
    svg_invalid_attrib(sp, ys, val);

  svg_stack_push(sp->curr->transforms, &cmd, sizeof(cmd));
}

static void
svg_parse_transform_skewX(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  struct svg_raster_cmd_transform_skew_x cmd = { .type = SVG_RASTER_CMD_TRANSFORM_SKEW_X };

  cmd.d = svg_parse_number(sp, ys, val);

  svg_stack_push(sp->curr->transforms, &cmd, sizeof(cmd));
}

static void
svg_parse_transform_skewY(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  struct svg_raster_cmd_transform_skew_y cmd = { .type = SVG_RASTER_CMD_TRANSFORM_SKEW_Y };

  cmd.d = svg_parse_number(sp, ys, val);

  svg_stack_push(sp->curr->transforms, &cmd, sizeof(cmd));
}

//
// PARSE RENDER STATE ATTRIBS -- FIXED LENGTH
//

static void
svg_parse_attrib_opacity(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  svg_attribs_save_scalar(sp, SVG_ATTRIB_TYPE_OPACITY);

  sp->curr->opacity = svg_parse_number(sp, ys, val);
}

static void
svg_parse_attrib_fill_rule(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  svg_attribs_save_scalar(sp, SVG_ATTRIB_TYPE_FILL_RULE);

  if (strcmp(val, "evenodd") == 0)
    sp->curr->fill_rule = SVG_FILL_RULE_OP_EVENODD;
  else if (strcmp(val, "nonzero") == 0)
    sp->curr->fill_rule = SVG_FILL_RULE_OP_NONZERO;
}

static void
svg_parse_color(struct svg_parser * sp,
                yxml_t *            ys,
                svg_paint_op *      op,
                svg_color_t *       color,
                char *              val,
                uint32_t            len)
{
  if (strcmp(val, "inherit") == 0)
    return;  // don't touch op or color

  if (strcmp(val, "none") == 0)
    {
      *op = SVG_PAINT_OP_NONE;  // don't touch color
      return;
    }

  // otherwise, parse it
  if (sscanf(val, "#%6x", color) == 1)
    {
      *op = SVG_PAINT_OP_COLOR;  // update op and color

      if (len == 4)
        {
          uint32_t const r = (*color >> 8) & 0xF;
          uint32_t const g = (*color >> 4) & 0xF;
          uint32_t const b = (*color >> 0) & 0xF;

          *color = SVG_RGB((r << 4) | r, (g << 4) | g, (b << 4) | b);
        }
      return;
    }

  {
    uint32_t r, g, b;
    if (sscanf(val, "rgb( %u , %u , %u )", &r, &g, &b) == 3)
      {
        *op    = SVG_PAINT_OP_COLOR;  // update op and color
        *color = SVG_RGB(r, g, b);
        return;
      }
  }

  {
    float r, g, b;
    if (sscanf(val, "rgb( %f%% , %f%% , %f%% )", &r, &g, &b) == 3)
      {
        *op    = SVG_PAINT_OP_COLOR;  // update op and color
        *color = SVG_RGB((uint32_t)(0xFF * r), (uint32_t)(0xFF * g), (uint32_t)(0xFF * b));
        return;
      }
  }

  // svg color keyword?
  struct svg_color_name const * cn = svg_color_name_lookup(val, len);

  if (cn != NULL)
    {
      *op    = SVG_PAINT_OP_COLOR;
      *color = cn->color;
      return;
    }

  //
  // FIXME(allanmac): parse floating point format
  //

  // otherwise this is an error
  svg_invalid_attrib(sp, ys, val);
}

static void
svg_parse_attrib_fill_color(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  // save even though it might not be changed
  svg_attribs_save_scalar(sp, SVG_ATTRIB_TYPE_FILL_OP);
  svg_attribs_save_scalar(sp, SVG_ATTRIB_TYPE_FILL_COLOR);

  //
  // parse color
  //
  // if "none"    then set flag to false
  // if "inherit" then do nothing
  // if color     then set flag to true and set color
  // else         error
  //
  svg_parse_color(sp, ys, &sp->curr->fill_op, &sp->curr->fill_color, val, len);
}

static void
svg_parse_attrib_fill_opacity(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  svg_attribs_save_scalar(sp, SVG_ATTRIB_TYPE_FILL_OPACITY);

  sp->curr->fill_opacity = svg_parse_number(sp, ys, val);
}

static void
svg_parse_attrib_stroke_color(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  // save even though it might not be changed
  svg_attribs_save_scalar(sp, SVG_ATTRIB_TYPE_STROKE_OP);
  svg_attribs_save_scalar(sp, SVG_ATTRIB_TYPE_STROKE_COLOR);

  //
  // parse color
  //
  // if "none"    then set flag to false
  // if "inherit" then do nothing
  // if color     then set flag to true and set color
  // else         error
  //
  svg_parse_color(sp, ys, &sp->curr->stroke_op, &sp->curr->stroke_color, val, len);
}

static void
svg_parse_attrib_stroke_opacity(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  svg_attribs_save_scalar(sp, SVG_ATTRIB_TYPE_STROKE_OPACITY);

  sp->curr->stroke_opacity = svg_parse_number(sp, ys, val);
}

static void
svg_parse_attrib_stroke_width(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
  svg_attribs_save_scalar(sp, SVG_ATTRIB_TYPE_STROKE_WIDTH);

  sp->curr->stroke_width = svg_parse_number(sp, ys, val);
}

//
//
//

static void
svg_attribs_dispatch(struct svg_parser * sp,
                     yxml_t *            ys,
                     const char *        attr_name,
                     const size_t        attr_len,
                     char *              attr_val,
                     const size_t        attr_val_len)
{
  const struct svg_attrib * r = svg_attrib_lookup(attr_name, (uint32_t)attr_len);

  //
  // warn or ignore
  //
  if (r == NULL)
    {
      svg_attrib_ignore(sp, ys, attr_name);
      return;
    }

  //
  // otherwise, process attribute
  //
  r->pfn(sp, ys, attr_val, (uint32_t)attr_val_len);
}

//
//
//

static void
svg_transform_dispatch(struct svg_parser * sp,
                       yxml_t *            ys,
                       const char *        attr_name,
                       const size_t        attr_len,
                       char *              attr_val,
                       const size_t        attr_val_len)
{
  const struct svg_transform * r = svg_transform_lookup(attr_name, (uint32_t)attr_len);

  //
  // error if not found
  //
  if (r == NULL)
    {
      svg_invalid_attrib(sp, ys, attr_name);
    }

  //
  // otherwise, process attribute
  //
  r->pfn(sp, ys, attr_val, (uint32_t)attr_val_len);

  //
  // save transform stack drop
  //
  svg_attribs_save(sp, SVG_ATTRIB_TYPE_TRANSFORM, 0);
}

//
//
//

static void
svg_parse_attrib_style(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
#define SVG_STYLE_NAME_LEN 33
#define SVG_STYLE_VALUE_LEN 65

  char name[SVG_STYLE_NAME_LEN];
  char value[SVG_STYLE_VALUE_LEN];

  do
    {
      int n = len;

      const int err = sscanf(val, " %32[^: \t\n] : %64[^;] %*[;]%n", name, value, &n);

      if (err < 2)
        svg_invalid_attrib(sp, ys, val);

      val += n;
      len -= n;

      svg_attribs_dispatch(sp, ys, name, strlen(name), value, strlen(value));
    }
  while (len > 0);
}

static void
svg_parse_attrib_transform(struct svg_parser * sp, yxml_t * ys, char * val, uint32_t len)
{
#define SVG_TRANSFORM_NAME_LEN 11
#define SVG_TRANSFORM_VALS_LEN 121

  char name[SVG_TRANSFORM_NAME_LEN], vals[SVG_TRANSFORM_VALS_LEN];

  do
    {
      int n = len;

      const int err = sscanf(val, " %10[^( \t\n] ( %120[^)] ) %n", name, vals, &n);

      if (err < 2)
        svg_invalid_attrib(sp, ys, val);

      val += n;
      len -= n;

      // fprintf(stderr,"%s ( %s )\n",name,vals);

      svg_transform_dispatch(sp, ys, name, strlen(name), vals, strlen(vals));
    }
  while (len > 0);
}

//
// ATTRIBS
//

#define SVG_ATTRIBS_LUT_ENTRY(name_, pfn_) { STRINGIFY_MACRO(name_), pfn_ },

static struct svg_attrib const svg_attribs_lut[] = {  //
  SVG_ATTRIBS_EXPAND(SVG_ATTRIBS_LUT_ENTRY)
};

static int
svg_attrib_cmp(void const * l, void const * r)
{
  char const * const              ls = l;
  struct svg_attrib const * const rs = r;

  return strcmp(ls, rs->name);
}

static struct svg_attrib const *
svg_attrib_lookup(char const * str, uint32_t len)
{
  //
  // FIXME(allanmac): this used to use a perfect hash
  //
  return bsearch(str,
                 svg_attribs_lut,
                 ARRAY_LENGTH_MACRO(svg_attribs_lut),
                 sizeof(svg_attribs_lut[0]),
                 svg_attrib_cmp);
}

//
// TRANSFORMS
//

#define SVG_TRANSFORMS_LUT_ENTRY(name_, pfn_) { STRINGIFY_MACRO(name_), pfn_ },

static struct svg_transform const svg_transforms_lut[] = {  //
  SVG_TRANSFORMS_EXPAND(SVG_TRANSFORMS_LUT_ENTRY)
};

static int
svg_transform_cmp(void const * l, void const * r)
{
  char const * const                 ls = l;
  struct svg_transform const * const rs = r;

  return strcmp(ls, rs->name);
}

static struct svg_transform const *
svg_transform_lookup(char const * str, uint32_t len)

{
  //
  // FIXME(allanmac): this used to use a perfect hash
  //
  return bsearch(str,
                 svg_transforms_lut,
                 ARRAY_LENGTH_MACRO(svg_transforms_lut),
                 sizeof(svg_transforms_lut[0]),
                 svg_transform_cmp);
}

//
// ELEMS
//

#define SVG_ELEMS_LUT_ENTRY(name_, pfn_) { STRINGIFY_MACRO(name_), pfn_ },

static struct svg_elem const svg_elems_lut[] = {  //
  SVG_ELEMS_EXPAND(SVG_ELEMS_LUT_ENTRY)
};

static int
svg_elem_cmp(void const * l, void const * r)
{
  char const * const            ls = l;
  struct svg_elem const * const rs = r;

  return strcmp(ls, rs->name);
}

static struct svg_elem const *
svg_elem_lookup(char const * str, uint32_t len)

{
  //
  // FIXME(allanmac): this used to use a perfect hash
  //
  return bsearch(str,
                 svg_elems_lut,
                 ARRAY_LENGTH_MACRO(svg_elems_lut),
                 sizeof(svg_elems_lut[0]),
                 svg_elem_cmp);
}

//
//
//

static void
svg_elem_begin(struct svg_parser * sp, yxml_t * ys)
{
  //
  // save undo count
  //
  svg_stack_push(sp->undo, &sp->undo_count, sizeof(sp->undo_count));

  //
  // reset count
  //
  sp->undo_count = 0;

  //
  // lookup element by name
  //
  const struct svg_elem * r = svg_elem_lookup(ys->elem, (uint32_t)yxml_symlen(ys, ys->elem));

  //
  // warn or ignore
  //
  if (r == NULL)
    {
      svg_warning(sp, ys, "unhandled element", ys->elem);
      return;
    }

  //
  // increment element count
  //
  sp->curr->elem_count += 1;  // svg_attribs_save_scalar(sp,SVG_ATTRIB_TYPE_ELEM_COUNT);

  //
  // otherwise, process element
  //
  r->pfn(sp, ys);
}

static void
svg_elem_end(struct svg_parser * sp)
{
  //
  // if necessary, compile any outstanding paths
  //
  compile(sp);

  //
  // apply undo stack for this element
  //
  svg_attribs_undo(sp);

  //
  // restore previous undo count
  //
  svg_attribs_restore_undo_count(sp);
}

//
//
//

static void
y_attr_copy(struct svg_parser * sp, const char * from)
{
  if (sp->attr_count + 8 > sp->attr_max)  // at most a few chars
    {
      sp->attr_max *= 2;
      sp->attr_buf = (char *)realloc(sp->attr_buf, sp->attr_max);
    }

  char c;

  while ((c = *from++) != 0)
    sp->attr_buf[sp->attr_count++] = c;
}

static void
y_attr_null_terminate(struct svg_parser * sp)
{
  sp->attr_buf[sp->attr_count] = 0;
}

static void
y_attr_reset(struct svg_parser * sp)
{
  sp->attr_count = 0;
}

//
//
//

static void
xml_parse(struct svg_parser * sp, yxml_t * ys, yxml_ret_t yr)
{
  static char * attr_cur;
  static size_t attr_len;

  switch (yr)
    {
      case YXML_OK:
        break;

      case YXML_ELEMSTART:
        // WRITE BEGIN COMMAND TO P/R/L DICTIONARIES
        svg_elem_begin(sp, ys);
        break;

      case YXML_CONTENT:
        break;

      case YXML_ELEMEND:
        // POP AND WRITE END COMMANDS TO P/R/L DICTIONARIES
        svg_elem_end(sp);
        break;

      case YXML_ATTRSTART:
        // SAVE ATTRIBUTE NAME
        attr_len = yxml_symlen(ys, ys->attr);  // save attr name len
        attr_cur = ys->attr;                   // save attr name
        y_attr_reset(sp);
        break;

      case YXML_ATTRVAL:
        y_attr_copy(sp, ys->data);
        break;

      case YXML_ATTREND:
        // PROCESS ATTRIBUTE AND WRITE BEGIN COMMANDS AND PUSH END
        // COMMANDS TO P/R/L STACKS
        y_attr_null_terminate(sp);
        svg_attribs_dispatch(sp,
                             ys,
                             attr_cur,
                             attr_len,
                             sp->attr_buf,
                             sp->attr_count);  // process attribute
        break;

      case YXML_PISTART:
      case YXML_PICONTENT:
      case YXML_PIEND:
        break;

      default:
        longjmp(svg_exception, yr);
    }
}

//
//
//

struct svg *
svg_parse(char const * doc, const bool is_verbose)
{
  //
  // init svg parser
  //
  struct svg_parser * sp = svg_parser_create(is_verbose);

  //
  // init YXML parser
  //
#define YXML_BUFFER_SIZE (1024 * 8)

  yxml_t * const ys = malloc(sizeof(*ys) + YXML_BUFFER_SIZE);

  yxml_init(ys, ys + 1, YXML_BUFFER_SIZE);

  //
  // default to failure
  //
  struct svg * sd = NULL;

  //
  // catch exceptions...
  //
  yxml_ret_t yr = 0;

  if ((yr = setjmp(svg_exception)) != YXML_OK)
    {
      if (sp->is_verbose)
        {
          fprintf(stderr, "Error: line %u, byte %lu -> error code: %d\n", ys->line, ys->byte, yr);
        }
    }
  else
    {
      //
      // parse until error or eof
      //
      while (true)
        {
          char const c = *doc++;

          if (c == 0)
            break;

          yr = yxml_parse(ys, c);

          xml_parse(sp, ys, yr);
        }

      // check for errors
      yr = yxml_eof(ys);

      if (yr < 0)
        {
          longjmp(svg_exception, yr);
        }

      // end any in-progress p/r/l's
      compile_end(sp);

      // create cmds struct
      sd = svg_create(sp);
    }

  // we're done with the YXML parser
  free(ys);

  // immediately dispose of parser
  svg_parser_dispose(sp);

  // done
  return sd;
}

//
// Returns NULL on failure
//

static char const *
svg_load(const char * const filename)
{
  // open file
  FILE * f = fopen(filename, "rb");

  if (f == NULL)
    {
      fprintf(stderr, "Error: fopen() - \"%s\"\n", filename);

      return NULL;
    }

  // seek to end of file
  if (fseek(f, 0L, SEEK_END) != 0)
    {
      fprintf(stderr, "Error: fseek() - \"%s\"\n", filename);

      (void)fclose(f);

      return NULL;
    }

  // get final position
  long const bytes = ftell(f);

  if (bytes == -1L)
    {
      fprintf(stderr, "Error: ftell() - \"%s\" %lu\n", filename, bytes);

      (void)fclose(f);

      return NULL;
    }

  // rewind to start
  rewind(f);

  // allocate
  char * const doc = malloc(bytes + 1);

  // read it in
  if (fread(doc, bytes, 1, f) != 1)
    {
      fprintf(stderr, "Error: fread() - \"%s\"\n", filename);

      free(doc);

      return NULL;
    }

  // close file
  if (fclose(f) != 0)
    {
      fprintf(stderr, "Error: close() - \"%s\"\n", filename);

      free(doc);

      return NULL;
    };

  // zero-terminate
  doc[bytes] = '\0';

  return doc;
}

//
// Returns NULL on failure
//

struct svg *
svg_open(char const * const filename, const bool is_verbose)
{
  // load entire SVG doc as a byte array
  char const * doc = svg_load(filename);

  if (doc == NULL)
    return NULL;

  // parse the svg doc and return the svg dictionary
  struct svg * const sd = svg_parse(doc, is_verbose);

  // dispose
  free((void *)doc);

  return sd;
}

//
//
//

#ifdef SVG_MAIN

#include <getopt.h>

int
main(int argc, char * argv[])
{
  //
  // defaults
  //
  bool is_verbose = true;

  //
  // process options
  //
  int opt;

  while ((opt = getopt(argc, argv, "hq")) != EOF)
    {
      switch (opt)
        {
          case 'h':
            fprintf(stderr, "This is a test...\n");
            return EXIT_FAILURE;

          case 'q':
            is_verbose = false;
            break;
        }
    }

  if (optind >= argc)
    {
      fprintf(stderr, "-- missing filename\n");
      return EXIT_FAILURE;  // no filename
    }

  //
  //
  //

  struct svg * sd = svg_open(argv[optind], is_verbose);

  if (sd == NULL)
    return EXIT_FAILURE;

  //
  //
  //

  fprintf(stderr,
          "p/r/l = %u / %u / %u\n",
          svg_path_count(sd),
          svg_raster_count(sd),
          svg_layer_count(sd));

  //
  //
  //

  svg_dispose(sd);

  return EXIT_SUCCESS;
}

#endif  // SVG_MAIN

//
//
//
