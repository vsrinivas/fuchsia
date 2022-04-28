// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "widget/widget.h"

#include <string.h>

#include "spinel/spinel_assert.h"
#include "spinel/spinel_opcodes.h"
#include "widget_defn.h"

//
// FIXME(allanmac): A single widget hierarchy walking function could
// probably collapse some of this code.
//

void
widget_destroy(struct widget *               widgets[],
               uint32_t                      widget_count,
               struct widget_context * const context)
{
  if ((widgets == NULL) || (widget_count == 0))
    return;

  for (uint32_t ii = 0; ii < widget_count; ii++)
    {
      struct widget * w = widgets[ii];

      w->pfn.destroy(w, context);
    }
}

//
//
//

void
widget_layout(struct widget *              widgets[],  //
              uint32_t                     widget_count,
              struct widget_layout * const layout,
              uint32_t * const             group_depth_max)
{
  *group_depth_max = 0;

  if ((widgets == NULL) || (widget_count == 0))
    return;

  for (uint32_t ii = 0; ii < widget_count; ii++)
    {
      struct widget * w = widgets[ii];

      w->pfn.layout(w, layout, group_depth_max);

      // update after layout
      layout->group.layer.count += w->layout.group.layer.count;
    }
}

//
//
//

void
widget_regen(struct widget *                     widgets[],
             uint32_t                            widget_count,
             struct widget_control const * const control,
             struct widget_context * const       context)
{
  if ((widgets == NULL) || (widget_count == 0))
    return;

  for (uint32_t ii = 0; ii < widget_count; ii++)
    {
      struct widget * w = widgets[ii];

      w->pfn.regen(w, control, context);
    }
}

//
//
//

bool
widget_event(struct widget *                    widgets[],
             uint32_t                           widget_count,
             struct widget_control * const      control,
             struct surface_event const * const event)
{
  if ((widgets == NULL) || (widget_count == 0))
    return true;

  for (uint32_t ii = 0; ii < widget_count; ii++)
    {
      struct widget * w = widgets[ii];

      if (!w->pfn.input(w, control, event))
        {
          return false;
        }
    }

  return true;
}

//
// So far widgets have the same layout calculation
//

void
widget_simple_impl_layout(struct widget *              widget,
                          struct widget_layout * const layout,
                          uint32_t * const             group_depth_max,
                          bool                         is_group,
                          uint32_t                     layer_count)
{
  if (is_group)
    {
      widget->layout.group.depth = layout->group.depth + 1;

      if (widget->layout.group.depth > *group_depth_max)
        {
          *group_depth_max = widget->layout.group.depth;
        }
    }
  else
    {
      widget->layout.group.depth = layout->group.depth;
    }

  // calculate this widget's layer base
  widget->layout.group.layer.base  = layout->group.layer.base + layout->group.layer.count;
  widget->layout.group.layer.count = layer_count;
}

//
// So far widgets have the same group definition
//

void
widget_simple_impl_styling_group(struct widget *                     widget,
                                 struct widget_control const * const control,
                                 struct widget_context * const       context,
                                 bool                                is_group,
                                 spinel_group_id * const             group_id)
{
  uint32_t const depth = widget->layout.group.depth;

  // is this a new group?
  if (is_group)
    {
      // allocate a group id
      spinel(styling_group_alloc(context->styling.curr, group_id));

      // convention is to save it into the parents array
      context->parents[depth] = *group_id;

      // declare parents leading back to root
      uint32_t * parents;

      spinel(styling_group_parents(context->styling.curr, *group_id, depth, &parents));

      // memcpy is noop if group_depth is 0
      memcpy(parents, context->parents, sizeof(*parents) * depth);

      // the range of this group is [layer_lo, layer_lo + layer_count - 1]
      spinel_layer_id const layer_lo = widget->layout.group.layer.base;
      spinel_layer_id const layer_hi = layer_lo + widget->layout.group.layer.count - 1;

      spinel(styling_group_range_lo(context->styling.curr, *group_id, layer_lo));
      spinel(styling_group_range_hi(context->styling.curr, *group_id, layer_hi));
    }
  else
    {
      *group_id = context->parents[depth];
    }
}

//
//
//

struct widget_input_args
{
  surface_input_pfn_t           input_pfn;
  void *                        data;
  struct widget **              widgets;
  uint32_t                      widget_count;
  struct widget_control * const control;
};

//
//
//

static void
widget_input_pfn(void * data, struct surface_event const * event)
{
  struct widget_input_args const * args = data;

  (void)widget_event(args->widgets, args->widget_count, args->control, event);

  if (args->input_pfn != NULL)
    {
      args->input_pfn(args->data, event);
    }
}

//
//
//

void
widget_surface_input(struct widget *               widgets[],
                     uint32_t                      widget_count,
                     struct widget_control * const control,
                     struct surface *              surface,
                     surface_input_pfn_t           input_pfn,
                     void *                        data)
{
  struct widget_input_args args = {

    // user-provided input event callback
    .input_pfn    = input_pfn,
    .data         = data,
    .widgets      = widgets,
    .widget_count = widget_count,
    .control      = control
  };

  surface_input(surface, widget_input_pfn, &args);

  //
  // end with a noop event -- necessary for now!
  //
  struct surface_event const event_noop = { .type = SURFACE_EVENT_TYPE_NOOP };

  widget_input_pfn(&args, &event_noop);
}

//
//
//

void
widget_regen_styling_root(struct widget_control const * const control,
                          struct widget_context * const       context,
                          struct widget_layout const * const  layout)
{
  // regenerate styling root?
  if (control->styling)
    {
      // allocate root group
      spinel(styling_group_alloc(context->styling.curr, context->parents + 0));

      spinel_group_id const group_id = context->parents[0];

      // root has no parents
      spinel(styling_group_parents(context->styling.curr, group_id, 0, NULL));

      // the range of this group is [0, layer_count - 1]
      spinel_layer_id lo = layout->group.layer.base;
      spinel_layer_id hi = lo + layout->group.layer.count - 1;

      spinel(styling_group_range_lo(context->styling.curr, group_id, lo));
      spinel(styling_group_range_hi(context->styling.curr, group_id, hi));

      //
      // enter
      //
      {
        spinel_styling_cmd_t * cmds_enter;

        spinel(styling_group_enter(context->styling.curr, group_id, 1, &cmds_enter));

        cmds_enter[0] = SPN_STYLING_OPCODE_COLOR_ACC_ZERO;
      }

      //
      // leave
      //
      {
        spinel_styling_cmd_t * cmds_leave;

        spinel(styling_group_leave(context->styling.curr, group_id, 4, &cmds_leave));

        // white for now
        float const rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        // cmds[0-2]
        spinel_styling_background_over_encoder(cmds_leave, rgba);

#ifdef __Fuchsia__  // assumes RGBA WSI surface
        cmds_leave[3] = SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE_RGBA8;
#else  // assumes BGRA WSI surface
        cmds_leave[3] = SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE_BGRA8;
#endif
      }
    }
}

//
//
//
