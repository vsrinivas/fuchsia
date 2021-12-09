// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_INCLUDE_WIDGET_WIDGET_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_INCLUDE_WIDGET_WIDGET_H_

//
//
//

#include "surface/surface.h"
#include "widget/widget_types.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

void
widget_destroy(struct widget *               widgets[],
               uint32_t                      widget_count,
               struct widget_context * const context);

//
//
//

void
widget_layout(struct widget *              widgets[],  //
              uint32_t                     widget_count,
              struct widget_layout * const layout,
              uint32_t * const             group_depth_max);

//
// Returns false if a widget signals that the event was consumed and is
// not expected to be propagated any further in the widget hierarchy.
//
// This is no more than a semantic hint that may ease composing widgets
// with other input-processing elements.
//

bool
widget_event(struct widget *                    widgets[],
             uint32_t                           widget_count,
             struct widget_control * const      control,
             struct surface_event const * const event);

//
//
//

#if 0
void
widget_timer(struct widget *                     widgets[],
             uint32_t                            widget_count,
             struct widget_control const * const control);
#endif

//
//
//

void
widget_regen(struct widget *                     widgets[],
             uint32_t                            widget_count,
             struct widget_control const * const control,
             struct widget_context * const       context);

//
// A convenience function that applies all available surface events to
// the widgets.
//

void
widget_surface_input(struct widget *               widgets[],
                     uint32_t                      widget_count,
                     struct widget_control * const control,
                     struct surface *              surface,
                     surface_input_pfn_t           input_pfn,
                     void *                        data);

//
// FIXME(allanmac): Set up the root styling group.  This can eventually
// be replaced by an explicit widget container.
//

void
widget_regen_styling_root(struct widget_control const * const control,
                          struct widget_context * const       context,
                          struct widget_layout const * const  layout);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_INCLUDE_WIDGET_WIDGET_H_
