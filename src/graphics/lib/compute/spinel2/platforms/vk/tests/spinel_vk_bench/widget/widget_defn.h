// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_WIDGET_DEFN_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_WIDGET_DEFN_H_

//
//
//

#include "widget/widget.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// widget definition
//

struct widget
{
  struct
  {
    void (*destroy)(struct widget *               widget,  //
                    struct widget_context * const context);

    void (*layout)(struct widget *              widget,
                   struct widget_layout * const layout,
                   uint32_t * const             group_depth_max);

    void (*regen)(struct widget *                     widget,
                  struct widget_control const * const control,
                  struct widget_context * const       context);

    bool (*input)(struct widget *                    widget,
                  struct widget_control * const      control,
                  struct surface_event const * const event);
  } pfn;

  struct widget_layout layout;
};

//
//
//

void
widget_simple_impl_layout(struct widget *              widget,
                          struct widget_layout * const layout,
                          uint32_t * const             group_depth_max,
                          bool                         is_group,
                          uint32_t                     layer_count);

//
//
//

void
widget_simple_impl_styling_group(struct widget *                     widget,
                                 struct widget_control const * const control,
                                 struct widget_context * const       context,
                                 bool                                is_group,
                                 spinel_group_id * const             group_id);
//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_WIDGET_DEFN_H_
