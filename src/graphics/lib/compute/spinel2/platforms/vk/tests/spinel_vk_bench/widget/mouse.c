// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "widget/mouse.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "common/macros.h"
#include "roboto_mono_regular.h"
#include "spinel/spinel_assert.h"
#include "spinel/spinel_opcodes.h"
#include "widget_defn.h"

//
//
//

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//
//
//

struct impl_vec2
{
  float x;
  float y;
};

//
//
//

union widget_mouse_paths
{
  struct
  {
    struct
    {
      spinel_path_t black;
      spinel_path_t white;
    } pointer;
  } named;

  struct
  {
    spinel_path_t handles[2];
    uint32_t      count;
  } extent;
};

//
//
//

union widget_mouse_rasters
{
  struct
  {
    struct
    {
      spinel_raster_t black;
      spinel_raster_t white;
    } pointer;
  } named;

  struct
  {
    spinel_raster_t handles[2];
    uint32_t        count;
    bool            is_valid;
  } extent;
};

//
//
//

struct widget_mouse
{
  struct widget              widget;
  struct impl_vec2           position;
  union widget_mouse_paths   paths;
  union widget_mouse_rasters rasters;

  //
  // FIXME(allanmac): Eventually decide whether or not the mouse always
  // (or never) creates its own styling group.
  //
  bool is_group;
};

//
//
//

static void
impl_paths_release(struct widget_mouse * impl, struct widget_context * const context)
{
  // free paths if the paths extent is valid
  if (impl->paths.extent.count > 0)
    {
      spinel(path_release(context->context,  //
                          impl->paths.extent.handles,
                          impl->paths.extent.count));

      impl->paths.extent.count = 0;
    }
}

static void
impl_rasters_release(struct widget_mouse * impl, struct widget_context * const context)
{
  // free rasters if the raster extent is valid
  if (impl->rasters.extent.count > 0)
    {
      spinel(raster_release(context->context,  //
                            impl->rasters.extent.handles,
                            impl->rasters.extent.count));

      impl->rasters.extent.count = 0;
    }
}

//
//
//

static void
impl_destroy(struct widget * widget, struct widget_context * const context)
{
  widget_mouse_t mouse = { .widget = widget };

  impl_paths_release(mouse.impl, context);

  impl_rasters_release(mouse.impl, context);

  free(mouse.impl);
}

//
//
//

static void
impl_layout(struct widget *              widget,
            struct widget_layout * const layout,
            uint32_t * const             group_depth_max)
{
  widget_mouse_t mouse = { .widget = widget };

  //
  // NOTE(allanmac): There is no advantage right now to
  // representing the mouse with its own child group.
  //
  widget_simple_impl_layout(widget, layout, group_depth_max, mouse.impl->is_group, 2);
}

//
//
//

static void
impl_regen(struct widget *                     widget,
           struct widget_control const * const control,
           struct widget_context * const       context)
{
  widget_mouse_t mouse = { .widget = widget };

  //
  // regen paths?
  //
  if (control->paths)
    {
      // release existing
      impl_paths_release(mouse.impl, context);

      //
      // mouse is a black pointer over a dilated white pointer
      //
      spinel_path_builder_t pb = context->pb;

      spinel(path_builder_begin(pb));
      spinel(path_builder_move_to(pb, +0.0f, 1.0f));
      spinel(path_builder_line_to(pb, -6.0f, 17.0f));
      spinel(path_builder_line_to(pb, +0.0f, 14.5f));
      spinel(path_builder_line_to(pb, +6.0f, 17.0f));
      spinel(path_builder_line_to(pb, +0.0f, 1.0f));
      spinel(path_builder_end(pb, &mouse.impl->paths.named.pointer.black));  // black

      spinel(path_builder_begin(pb));
      spinel(path_builder_move_to(pb, +0.0f, 0.0f));
      spinel(path_builder_line_to(pb, -7.0f, 18.0f));
      spinel(path_builder_line_to(pb, +0.0f, 15.0f));
      spinel(path_builder_line_to(pb, +7.0f, 18.0f));
      spinel(path_builder_line_to(pb, +0.0f, 0.0f));
      spinel(path_builder_end(pb, &mouse.impl->paths.named.pointer.white));  // white

      mouse.impl->paths.extent.count = ARRAY_LENGTH_MACRO(mouse.impl->paths.extent.handles);
    }

  //
  // regen rasters?
  //
  // FIXME(allanmac): raster translation isn't available yet
  //
  if (control->rasters && !mouse.impl->rasters.extent.is_valid)
    {
      assert(mouse.impl->paths.extent.count != 0);

      // release existing
      impl_rasters_release(mouse.impl, context);

      // create new
      struct spinel_transform_stack * const ts = context->ts;
      spinel_raster_builder_t               rb = context->rb;

      // update transform stack
      uint32_t const ts_save = spinel_transform_stack_save(ts);

      spinel_transform_stack_push_translate(ts, mouse.impl->position.x, mouse.impl->position.y);
      spinel_transform_stack_concat(ts);

      spinel_transform_stack_push_rotate(ts, (float)(-M_PI / 6.0));
      spinel_transform_stack_concat(ts);

      spinel_transform_t const * tos = spinel_transform_stack_top_transform(ts);

      static struct spinel_clip const raster_clips[] = { { 0.0f, 0.0f, FLT_MAX, FLT_MAX } };

      // build rasters
      spinel(raster_builder_begin(rb));
      spinel(raster_builder_add(rb,  //
                                &mouse.impl->paths.named.pointer.black,
                                NULL,
                                tos,
                                NULL,
                                raster_clips,
                                1));
      spinel(raster_builder_end(rb, &mouse.impl->rasters.named.pointer.black));

      spinel(raster_builder_begin(rb));
      spinel(raster_builder_add(rb,  //
                                &mouse.impl->paths.named.pointer.white,
                                NULL,
                                tos,
                                NULL,
                                raster_clips,
                                1));
      spinel(raster_builder_end(rb, &mouse.impl->rasters.named.pointer.white));

      // restore transform stack
      spinel_transform_stack_restore(ts, ts_save);

      mouse.impl->rasters.extent.count = ARRAY_LENGTH_MACRO(mouse.impl->rasters.extent.handles);

      // now valid for current position
      mouse.impl->rasters.extent.is_valid = true;
    }

  //
  // regen styling?
  //
  if (control->styling)
    {
      spinel_group_id group_id;

      widget_simple_impl_styling_group(mouse.widget,
                                       control,
                                       context,
                                       mouse.impl->is_group,
                                       &group_id);

      spinel_layer_id const layer_lo = mouse.widget->layout.group.layer.base;

      //
      // declare styling commands for mouse layers 0 and 1
      //
      spinel_styling_cmd_t cmds_from[] = {

        [0] = SPN_STYLING_OPCODE_COVER_NONZERO,
        // [1][2][3] is rgba
        [4] = SPN_STYLING_OPCODE_BLEND_OVER
      };

      {
        float const rgba[4] = { 0.0f, 0.0f, 0.0f, 1.0f };  // fill solid black

        spinel_styling_layer_fill_rgba_encoder(cmds_from + 1, rgba);  // cmds[1..3]

        spinel_styling_cmd_t * cmds_to;

        spinel(styling_group_layer(context->styling.curr, group_id, layer_lo + 0, 5, &cmds_to));

        memcpy(cmds_to, cmds_from, sizeof(cmds_from));
      }
      {
        float const rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };  // fill solid white

        spinel_styling_layer_fill_rgba_encoder(cmds_from + 1, rgba);  // cmds[1..3]

        spinel_styling_cmd_t * cmds_to;

        spinel(styling_group_layer(context->styling.curr, group_id, layer_lo + 1, 5, &cmds_to));

        memcpy(cmds_to, cmds_from, sizeof(cmds_from));
      }
    }

  //
  // regen composition?
  //
  if (control->composition)
    {
      assert(mouse.impl->rasters.extent.count != 0);

      spinel_layer_id layer_id = mouse.widget->layout.group.layer.base;

      spinel(composition_place(context->composition.curr,
                               &mouse.impl->rasters.named.pointer.black,
                               &layer_id,
                               NULL,
                               1));

      ++layer_id;

      spinel(composition_place(context->composition.curr,
                               &mouse.impl->rasters.named.pointer.white,
                               &layer_id,
                               NULL,
                               1));
    }
}

//
// NOTE(allanmac): For now, moving the mouse forces local regen of the
// rasters and global regen of the styling and composition.  This will
// change when the composition and styling are incrementally updatable.
//

static void
impl_rerasterize(widget_mouse_t * const mouse, struct widget_control * const control)
{
  mouse->impl->rasters.extent.is_valid = false;

  control->rasters     = true;
  control->styling     = true;
  control->composition = true;
  control->render      = true;
}

//
//
//

static bool
impl_input(struct widget *                    widget,
           struct widget_control * const      control,
           struct surface_event const * const event)
{
  widget_mouse_t mouse = { .widget = widget };

  // process input
  switch (event->type)
    {
        case SURFACE_EVENT_TYPE_POINTER_INPUT: {
          mouse.impl->position.x = (float)event->pointer.x;
          mouse.impl->position.y = (float)event->pointer.y;

          impl_rerasterize(&mouse, control);
          break;
        }

        case SURFACE_EVENT_TYPE_TOUCH_INPUT: {
          if (event->touch.contact_count.curr == 1)
            {
              float const x =
                (float)((event->touch.extent.width *
                         (event->touch.contacts[0].x - event->touch.contact_axes.x.min)) /
                        (event->touch.contact_axes.x.max - event->touch.contact_axes.x.min));

              float const y =
                (float)((event->touch.extent.height *
                         (event->touch.contacts[0].y - event->touch.contact_axes.y.min)) /
                        (event->touch.contact_axes.y.max - event->touch.contact_axes.y.min));

              mouse.impl->position.x = x;
              mouse.impl->position.y = y;

              impl_rerasterize(&mouse, control);
            }
          else if (event->touch.contact_count.curr == 2)
            {
              //
              // Move the mouse to the center of two contacts
              //
              float const x0 =
                (float)((event->touch.extent.width *
                         (event->touch.contacts[0].x - event->touch.contact_axes.x.min)) /
                        (event->touch.contact_axes.x.max - event->touch.contact_axes.x.min));

              float const y0 =
                (float)((event->touch.extent.height *
                         (event->touch.contacts[0].y - event->touch.contact_axes.y.min)) /
                        (event->touch.contact_axes.y.max - event->touch.contact_axes.y.min));

              float const x1 =
                (float)((event->touch.extent.width *
                         (event->touch.contacts[1].x - event->touch.contact_axes.x.min)) /
                        (event->touch.contact_axes.x.max - event->touch.contact_axes.x.min));

              float const y1 =
                (float)((event->touch.extent.height *
                         (event->touch.contacts[1].y - event->touch.contact_axes.y.min)) /
                        (event->touch.contact_axes.y.max - event->touch.contact_axes.y.min));

              mouse.impl->position.x = (x0 + x1) * 0.5f;
              mouse.impl->position.y = (y0 + y1) * 0.5f;

              impl_rerasterize(&mouse, control);
            }
          break;
        }

        default: {
          break;
        }
    }

  return true;
}

//
//
//

widget_mouse_t
widget_mouse_create()
{
  widget_mouse_t widget_mouse = { .impl = malloc(sizeof(*widget_mouse.impl)) };

  // use a designated initializer
  *widget_mouse.impl = (struct widget_mouse){

    .widget = {
      .pfn = {
        .destroy = impl_destroy,
        .layout  = impl_layout,
        .regen   = impl_regen,
        .input   = impl_input,
      },
    },

    .is_group = false
  };

  return widget_mouse;
}

//
//
//
