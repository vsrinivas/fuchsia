// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "widget/coords.h"

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

union widget_coords_paths
{
  struct
  {
    spinel_path_t paren_left;
    spinel_path_t paren_right;
    spinel_path_t comma;

    spinel_path_t digits[10];
  } named;

  struct
  {
    spinel_path_t handles[3 + 10];
    uint32_t      count;
  } extent;
};

//
//
//

struct widget_coords_rasters
{
  struct
  {
    spinel_raster_t handles[1];
    uint32_t        count;
    bool            is_valid;
  } extent;
};

//
//
//

struct widget_coords
{
  struct widget                widget;
  struct impl_vec2             position;
  union widget_coords_paths    paths;
  struct widget_coords_rasters rasters;
  float                        glyph_width;

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
impl_paths_release(struct widget_coords * impl, struct widget_context * const context)
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
impl_rasters_release(struct widget_coords * impl, struct widget_context * const context)
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
  widget_coords_t coords = { .widget = widget };

  impl_paths_release(coords.impl, context);

  impl_rasters_release(coords.impl, context);

  free(coords.impl);
}

//
//
//

static void
impl_layout(struct widget *              widget,
            struct widget_layout * const layout,
            uint32_t * const             group_depth_max)
{
  widget_coords_t coords = { .widget = widget };

  //
  // NOTE(allanmac): There is no advantage right now to
  // representing the mouse with its own child group.
  //
  widget_simple_impl_layout(widget, layout, group_depth_max, coords.impl->is_group, 2);
}

//
//
//
static void
impl_rasterize_digits(widget_coords_t *             coords,
                      struct widget_context * const context,
                      struct spinel_clip const      raster_clips[],
                      struct font_metrics const *   metrics,
                      uint32_t *                    char_count,
                      uint32_t                      number,
                      uint32_t                      divisor)
{
  uint32_t const quot = number / divisor;

  if ((quot == 0) && (divisor > 1))
    {
      return;
    }

  struct spinel_transform_stack * const ts = context->ts;

  spinel_transform_stack_push_translate(ts, (float)(metrics->advance.width * ++*char_count), 0);
  spinel_transform_stack_concat(ts);

  uint32_t const digit = quot % 10;

  spinel(raster_builder_add(context->rb,  //
                            coords->impl->paths.named.digits + digit,
                            NULL,
                            spinel_transform_stack_top_transform(ts),
                            NULL,
                            raster_clips,
                            1));

  spinel_transform_stack_drop(ts);
}

//
//
//

static void
impl_regen(struct widget *                     widget,
           struct widget_control const * const control,
           struct widget_context * const       context)
{
  widget_coords_t coords = { .widget = widget };

  //
  // regen paths?
  //
  if (control->paths)
    {
      // release existing
      impl_paths_release(coords.impl, context);

      // define new
      spinel_path_builder_t pb = context->pb;

      // clang-format off
      FONT_GLYPH_PFN(roboto_mono_regular, paren_left) (pb, &coords.impl->paths.named.paren_left);
      FONT_GLYPH_PFN(roboto_mono_regular, paren_right)(pb, &coords.impl->paths.named.paren_right);
      FONT_GLYPH_PFN(roboto_mono_regular, comma)      (pb, &coords.impl->paths.named.comma);
      // clang-format on

      // clang-format off
      FONT_GLYPH_PFN(roboto_mono_regular, zero) (pb, coords.impl->paths.named.digits + 0);
      FONT_GLYPH_PFN(roboto_mono_regular, one)  (pb, coords.impl->paths.named.digits + 1);
      FONT_GLYPH_PFN(roboto_mono_regular, two)  (pb, coords.impl->paths.named.digits + 2);
      FONT_GLYPH_PFN(roboto_mono_regular, three)(pb, coords.impl->paths.named.digits + 3);
      FONT_GLYPH_PFN(roboto_mono_regular, four) (pb, coords.impl->paths.named.digits + 4);
      FONT_GLYPH_PFN(roboto_mono_regular, five) (pb, coords.impl->paths.named.digits + 5);
      FONT_GLYPH_PFN(roboto_mono_regular, six)  (pb, coords.impl->paths.named.digits + 6);
      FONT_GLYPH_PFN(roboto_mono_regular, seven)(pb, coords.impl->paths.named.digits + 7);
      FONT_GLYPH_PFN(roboto_mono_regular, eight)(pb, coords.impl->paths.named.digits + 8);
      FONT_GLYPH_PFN(roboto_mono_regular, nine) (pb, coords.impl->paths.named.digits + 9);
      // clang-format on

      coords.impl->paths.extent.count = ARRAY_LENGTH_MACRO(coords.impl->paths.extent.handles);
    }

  //
  // regen rasters?
  //
  // FIXME(allanmac): raster translation isn't available yet
  //
  if (control->rasters && !coords.impl->rasters.extent.is_valid)
    {
      assert(coords.impl->paths.extent.count != 0);

      // release existing
      impl_rasters_release(coords.impl, context);

      // create new
      struct spinel_transform_stack * const ts = context->ts;
      spinel_raster_builder_t               rb = context->rb;

      // update transform stack
      uint32_t const ts_save = spinel_transform_stack_save(ts);

      // position the "(x,y)" string
      spinel_transform_stack_push_translate(ts, coords.impl->position.x, coords.impl->position.y);
      spinel_transform_stack_concat(ts);

      // get mono metrics
      struct font_metrics metrics;

      FONT_METRICS_PFN(roboto_mono_regular)(&metrics);

      // size the glyphs
      float const scale = coords.impl->glyph_width / (float)metrics.advance.width;

      spinel_transform_stack_push_scale(ts, scale, -scale);
      spinel_transform_stack_concat(ts);

      spinel_transform_stack_push_translate(ts, 0.0f, (float)metrics.descent);
      spinel_transform_stack_concat(ts);

      static struct spinel_clip const raster_clips[] = { { 0.0f, 0.0f, FLT_MAX, FLT_MAX } };

      //
      // build single raster: "(<x>,<y>)"
      //
      spinel(raster_builder_begin(rb));

      uint32_t char_count = 0;

      // PAREN LEFT
      {
        spinel(raster_builder_add(rb,  //
                                  &coords.impl->paths.named.paren_left,
                                  NULL,
                                  spinel_transform_stack_top_transform(ts),
                                  NULL,
                                  raster_clips,
                                  1));
      }

      // X-COORD
      {
        uint32_t x = MIN_MACRO(uint32_t, (uint32_t)coords.impl->position.x, 9999);

        impl_rasterize_digits(&coords, context, raster_clips, &metrics, &char_count, x, 1000);
        impl_rasterize_digits(&coords, context, raster_clips, &metrics, &char_count, x, 100);
        impl_rasterize_digits(&coords, context, raster_clips, &metrics, &char_count, x, 10);
        impl_rasterize_digits(&coords, context, raster_clips, &metrics, &char_count, x, 1);
      }

      // COMMA
      {
        spinel_transform_stack_push_translate(ts, (float)(metrics.advance.width * ++char_count), 0);
        spinel_transform_stack_concat(ts);

        spinel(raster_builder_add(rb,  //
                                  &coords.impl->paths.named.comma,
                                  NULL,
                                  spinel_transform_stack_top_transform(ts),
                                  NULL,
                                  raster_clips,
                                  1));

        spinel_transform_stack_drop(ts);
      }

      // Y-COORD
      {
        uint32_t y = MIN_MACRO(uint32_t, (uint32_t)coords.impl->position.y, 9999);

        impl_rasterize_digits(&coords, context, raster_clips, &metrics, &char_count, y, 1000);
        impl_rasterize_digits(&coords, context, raster_clips, &metrics, &char_count, y, 100);
        impl_rasterize_digits(&coords, context, raster_clips, &metrics, &char_count, y, 10);
        impl_rasterize_digits(&coords, context, raster_clips, &metrics, &char_count, y, 1);
      }

      // PAREN RIGHT
      {
        spinel_transform_stack_push_translate(ts, (float)(metrics.advance.width * ++char_count), 0);
        spinel_transform_stack_concat(ts);

        spinel(raster_builder_add(rb,  //
                                  &coords.impl->paths.named.paren_right,
                                  NULL,
                                  spinel_transform_stack_top_transform(ts),
                                  NULL,
                                  raster_clips,
                                  1));

        spinel_transform_stack_drop(ts);
      }

      spinel(raster_builder_end(rb, coords.impl->rasters.extent.handles));

      coords.impl->rasters.extent.count = 1;

      // now valid for current position
      coords.impl->rasters.extent.is_valid = true;

      // restore transform stack
      spinel_transform_stack_restore(ts, ts_save);
    }

  //
  // regen styling?
  //
  if (control->styling)
    {
      spinel_group_id group_id;

      widget_simple_impl_styling_group(coords.widget,
                                       control,
                                       context,
                                       coords.impl->is_group,
                                       &group_id);

      spinel_layer_id const layer_lo = coords.widget->layout.group.layer.base;

      //
      // declare styling commands for mouse layers 0 and 1
      //
      spinel_styling_cmd_t cmds_from[] = {

        [0] = SPN_STYLING_OPCODE_COVER_NONZERO,
        // [1][2][3] is rgba
        [4] = SPN_STYLING_OPCODE_BLEND_OVER
      };

      float const rgba[4] = { 0.0f, 0.0f, 0.0f, 1.0f };  // fill solid black

      spinel_styling_layer_fill_rgba_encoder(cmds_from + 1, rgba);  // cmds[1..3]

      spinel_styling_cmd_t * cmds_to;

      spinel(styling_group_layer(context->styling.curr, group_id, layer_lo + 0, 5, &cmds_to));

      memcpy(cmds_to, cmds_from, sizeof(cmds_from));
    }

  //
  // regen composition?
  //
  if (control->composition)
    {
      assert(coords.impl->rasters.extent.count != 0);

      spinel_layer_id layer_id = coords.widget->layout.group.layer.base;

      spinel(composition_place(context->composition.curr,
                               coords.impl->rasters.extent.handles,
                               &layer_id,
                               NULL,
                               coords.impl->rasters.extent.count));
    }
}

//
// NOTE(allanmac): For now, moving the mouse forces local regen of the
// rasters and global regen of the styling and composition.  This will
// change when the composition and styling are incrementally updatable.
//

static void
impl_rerasterize(widget_coords_t * const coords, struct widget_control * const control)
{
  coords->impl->rasters.extent.is_valid = false;

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
  widget_coords_t coords = { .widget = widget };

  // process input
  switch (event->type)
    {
        case SURFACE_EVENT_TYPE_POINTER_INPUT: {
          coords.impl->position.x = (float)event->pointer.x;
          coords.impl->position.y = (float)event->pointer.y;

          impl_rerasterize(&coords, control);
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

              coords.impl->position.x = x;
              coords.impl->position.y = y;

              impl_rerasterize(&coords, control);
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

              coords.impl->position.x = (x0 + x1) * 0.5f;
              coords.impl->position.y = (y0 + y1) * 0.5f;

              impl_rerasterize(&coords, control);
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

widget_coords_t
widget_coords_create(float glyph_width)
{
  widget_coords_t widget_coords = { .impl = malloc(sizeof(*widget_coords.impl)) };

  // use a designated initializer
  *widget_coords.impl = (struct widget_coords){

    .widget = {
      .pfn = {
        .destroy = impl_destroy,
        .layout  = impl_layout,
        .regen   = impl_regen,
        .input   = impl_input,
      },
    },

    .glyph_width = glyph_width,

    .is_group = false
  };

  return widget_coords;
}

//
//
//
