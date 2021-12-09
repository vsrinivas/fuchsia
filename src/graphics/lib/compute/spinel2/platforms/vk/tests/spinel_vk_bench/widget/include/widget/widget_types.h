// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_INCLUDE_WIDGET_WIDGET_TYPES_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_INCLUDE_WIDGET_WIDGET_TYPES_H_

//
//
//

#include "spinel/ext/transform_stack/transform_stack.h"
#include "spinel/spinel.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

struct widget;

//
//
//

#define WIDGET_TYPE(impl_) impl_##_t

#define WIDGET_TYPEDEF(impl_)                                                                      \
  typedef union impl_##_u                                                                          \
  {                                                                                                \
    struct widget * widget;                                                                        \
    struct impl_ *  impl;                                                                          \
  } WIDGET_TYPE(impl_)

//
// Propagate the depth and tight layer requirements
//

struct widget_layout
{
  struct
  {
    uint32_t depth;

    struct
    {
      spinel_layer_id base;
      uint32_t        count;
    } layer;

  } group;
};

//
// Global control flags indicating what to regenerate.
//
// NOTE(allanmac): In some situations, a widget could ignore these hints
// but, for now, a flag indicating that the widget's styling or
// composition must be regenerated cannot be ignored because these two
// containers don't yet support incremental updates.
//
// NOTE(allanmac): This idiom can mostly be removed once the Spinel API
// is updated.
//

struct widget_control
{
  union
  {
    uint32_t flags;
    struct
    {
      // clang-format off
      bool paths       : 1;
      bool rasters     : 1;
      bool styling     : 1;
      bool composition : 1;
      bool render      : 1;
      // clang-format on
    };
  };
};

//
//
//

#define WIDGET_CONTROL_PRSCR()                                                                     \
  (struct widget_control)                                                                          \
  {                                                                                                \
    .paths = true, .rasters = true, .styling = true, .composition = true, .render = true           \
  }

#define WIDGET_CONTROL_RSCR()                                                                      \
  (struct widget_control)                                                                          \
  {                                                                                                \
    .paths = false, .rasters = true, .styling = true, .composition = true, .render = true          \
  }

#define WIDGET_CONTROL_SCR()                                                                       \
  (struct widget_control)                                                                          \
  {                                                                                                \
    .paths = false, .rasters = false, .styling = true, .composition = true, .render = true         \
  }

#define WIDGET_CONTROL_R()                                                                         \
  (struct widget_control)                                                                          \
  {                                                                                                \
    .paths = false, .rasters = false, .styling = false, .composition = false, .render = true       \
  }

#define WIDGET_CONTROL_NOOP()                                                                      \
  (struct widget_control)                                                                          \
  {                                                                                                \
    .paths = false, .rasters = false, .styling = false, .composition = false, .render = false      \
  }

//
//
//

struct widget_context
{
  // clang-format off
  spinel_context_t                context;

  spinel_path_builder_t           pb;
  spinel_raster_builder_t         rb;

  struct spinel_transform_stack * ts;

  struct
  {
    spinel_styling_t              prev;
    spinel_styling_t              curr;
  } styling;

  struct
  {
    spinel_composition_t          prev;
    spinel_composition_t          curr;
  } composition;

  spinel_group_id *               parents;
  // clang-format on
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

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_INCLUDE_WIDGET_WIDGET_TYPES_H_
