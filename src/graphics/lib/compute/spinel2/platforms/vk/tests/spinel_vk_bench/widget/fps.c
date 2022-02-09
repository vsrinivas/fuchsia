// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "widget/fps.h"

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common/macros.h"
#include "roboto_mono_regular.h"
#include "spinel/spinel_assert.h"
#include "spinel/spinel_opcodes.h"
#include "widget_defn.h"

//
//
//

struct widget_fps
{
  struct widget widget;

  float glyph_width;

  uint64_t timestamps[3];
  uint64_t frames[2];
  uint32_t fps;
  bool     is_quiet;

  struct
  {
    spinel_path_t extent[10];
    uint32_t      count;
  } paths;

  struct
  {
    spinel_raster_t extent[4 * 10];
    uint32_t        count;
  } rasters;

  //
  // FIXME(allanmac): Eventually decide whether or not the fps always
  // (or never) creates its own styling group.
  //
  bool is_group;
};

//
//
//

static uint64_t
impl_timestamp()
{
  struct timespec ts;

  timespec_get(&ts, TIME_UTC);

  uint64_t const timestamp = ts.tv_sec * 1000000000UL + ts.tv_nsec;

  return timestamp;
}

//
//
//

#define SPN_FPS_PERIOD ((uint64_t)2e9)  // 2 secs.

static void
impl_fps(struct widget_fps * impl, struct widget_control const * const control)
{
  impl->timestamps[2] = impl_timestamp();

  impl->frames[1] += 1;

  double const delta = (double)(impl->timestamps[2] - impl->timestamps[1]);

  if (delta >= SPN_FPS_PERIOD)
    {
      double const fps = (1e+9 * (double)impl->frames[1]) / delta;

      impl->frames[0] += impl->frames[1];

      if (!impl->is_quiet)
        {
          double const elapsed = (double)(impl->timestamps[2] - impl->timestamps[0]);

          char const pls[6] = {

            // clang-format off
            control->paths       ? 'P' : '.',
            control->rasters     ? 'R' : '.',
            control->styling     ? 'S' : '.',
            control->composition ? 'C' : '.',
            control->render      ? 'R' : '.',
            0
            // clang-format on
          };

          double const secs = elapsed / 1e+9;
          double const hh   = floor(secs / 3600.0);
          double const mm   = floor((secs - (hh * 3600.0)) / 60.0);
          double const ss   = secs - (hh * 3600) - (mm * 60);

          fprintf(stderr,
                  "HH:MM:SS/TotalFrames/PeriodFrames/FrameMsecs/FPS[%s]: "
                  "%05.0f:%02.0f:%02.0f, "
                  "%10lu, %5lu, %7.3f, %.1f\n",
                  pls,
                  hh,
                  mm,
                  ss,
                  impl->frames[0],
                  impl->frames[1],
                  delta / (1e+6 * (double)impl->frames[1]),
                  fps);
        }

      impl->fps           = (uint32_t)round(fps);
      impl->timestamps[1] = impl->timestamps[2];
      impl->frames[1]     = 0UL;

      //
      // FIXME(allanmac): this needs to run on a timer
      //
      // control->composition = true;
      //
    }
}

//
//
//

static void
impl_paths_release(struct widget_fps * impl, struct widget_context * const context)
{
  // free paths if the paths extent is valid
  if (impl->paths.count > 0)
    {
      spinel(path_release(context->context,  //
                          impl->paths.extent,
                          ARRAY_LENGTH_MACRO(impl->paths.extent)));

      impl->paths.count = 0;
    }
}

static void
impl_rasters_release(struct widget_fps * impl, struct widget_context * const context)
{
  // free rasters if the raster extent is valid
  if (impl->rasters.count > 0)
    {
      spinel(raster_release(context->context,
                            impl->rasters.extent,
                            ARRAY_LENGTH_MACRO(impl->rasters.extent)));

      impl->rasters.count = 0;
    }
}

//
//
//

static void
impl_destroy(struct widget * widget, struct widget_context * const context)
{
  widget_fps_t fps = { .widget = widget };

  impl_paths_release(fps.impl, context);

  impl_rasters_release(fps.impl, context);

  free(fps.impl);
}

//
//
//

static void
impl_layout(struct widget *              widget,
            struct widget_layout * const layout,
            uint32_t * const             group_depth_max)
{
  widget_fps_t fps = { .widget = widget };

  //
  // NOTE(allanmac): There is no advantage right now to
  // representing the fps counter with its own child group.
  //
  widget_simple_impl_layout(widget, layout, group_depth_max, fps.impl->is_group, 1);
}

//
//
//

static void
impl_regen(struct widget *                     widget,
           struct widget_control const * const control,
           struct widget_context * const       context)
{
  widget_fps_t fps = { .widget = widget };

  // check timer and update fps
  impl_fps(fps.impl, control);

  //
  // regen paths?
  //
  if (control->paths)
    {
      // release existing
      impl_paths_release(fps.impl, context);

      //
      // fps is a black pointer over a dilated white pointer
      //
      spinel_path_builder_t pb = context->pb;

      // clang-format off
      FONT_GLYPH_PFN(roboto_mono_regular, zero) (pb, fps.impl->paths.extent + 0);
      FONT_GLYPH_PFN(roboto_mono_regular, one)  (pb, fps.impl->paths.extent + 1);
      FONT_GLYPH_PFN(roboto_mono_regular, two)  (pb, fps.impl->paths.extent + 2);
      FONT_GLYPH_PFN(roboto_mono_regular, three)(pb, fps.impl->paths.extent + 3);
      FONT_GLYPH_PFN(roboto_mono_regular, four) (pb, fps.impl->paths.extent + 4);
      FONT_GLYPH_PFN(roboto_mono_regular, five) (pb, fps.impl->paths.extent + 5);
      FONT_GLYPH_PFN(roboto_mono_regular, six)  (pb, fps.impl->paths.extent + 6);
      FONT_GLYPH_PFN(roboto_mono_regular, seven)(pb, fps.impl->paths.extent + 7);
      FONT_GLYPH_PFN(roboto_mono_regular, eight)(pb, fps.impl->paths.extent + 8);
      FONT_GLYPH_PFN(roboto_mono_regular, nine) (pb, fps.impl->paths.extent + 9);
      // clang-format on

      fps.impl->paths.count = ARRAY_LENGTH_MACRO(fps.impl->paths.extent);
    }

  //
  // regen rasters?
  //
  // FIXME(allanmac): raster translation isn't available yet but we work around
  // that (for now) by rasterizing 10 numbers at all four digit positions.
  //
  if (control->rasters)
    {
      assert(fps.impl->paths.count != 0);

      // release existing
      impl_rasters_release(fps.impl, context);

      // create new
      struct spinel_transform_stack * const ts = context->ts;
      spinel_raster_builder_t               rb = context->rb;

      // update transform stack
      uint32_t const ts_save = spinel_transform_stack_save(ts);

      // get mono metrics
      struct font_metrics metrics;

      FONT_METRICS_PFN(roboto_mono_regular)(&metrics);

      // size the glyphs
      float const scale = fps.impl->glyph_width / (float)metrics.advance.width;

      spinel_transform_stack_push_scale(ts, scale, -scale);
      spinel_transform_stack_concat(ts);

      spinel_transform_stack_push_translate(ts, 0.0f, (float)-metrics.ascent);
      spinel_transform_stack_concat(ts);

      static struct spinel_clip const raster_clips[] = { { 0.0f, 0.0f, FLT_MAX, FLT_MAX } };

      // redefine all rasters
      for (uint32_t ii = 0; ii < ARRAY_LENGTH_MACRO(fps.impl->rasters.extent) / 10; ii++)
        {
          spinel_transform_stack_push_translate(ts, (float)(metrics.advance.width * ii), 0);
          spinel_transform_stack_concat(ts);

          for (uint32_t jj = 0; jj < 10; jj++)
            {
              spinel(raster_builder_begin(rb));
              spinel(raster_builder_add(rb,
                                        fps.impl->paths.extent + jj,
                                        NULL,
                                        spinel_transform_stack_top_transform(ts),
                                        NULL,
                                        raster_clips,
                                        1));
              spinel(raster_builder_end(rb, fps.impl->rasters.extent + ii * 10 + jj));
            }

          spinel_transform_stack_drop(ts);
        }

      // restore transform stack
      spinel_transform_stack_restore(ts, ts_save);

      fps.impl->rasters.count = ARRAY_LENGTH_MACRO(fps.impl->rasters.extent);
    }

  //
  // regen styling?
  //
  if (control->styling)
    {
      spinel_group_id group_id;

      widget_simple_impl_styling_group(fps.widget, control, context, fps.impl->is_group, &group_id);

      spinel_layer_id const layer_lo = fps.widget->layout.group.layer.base;

      //
      // declare styling commands for fps layers 0 and 1
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
      assert(fps.impl->rasters.count != 0);

      spinel_layer_id layer_lo = fps.widget->layout.group.layer.base;
      uint32_t        fps_quot = fps.impl->fps;
      uint32_t        digits   = ARRAY_LENGTH_MACRO(fps.impl->rasters.extent) / 10;

      while (digits-- > 0)
        {
          uint32_t const digit = fps_quot % 10;

          spinel(composition_place(context->composition.curr,
                                   fps.impl->rasters.extent + digits * 10 + digit,
                                   &layer_lo,
                                   NULL,
                                   1));

          fps_quot /= 10;

          if (fps_quot == 0)
            {
              break;
            }
        }
    }
}

//
// Input events are ignored
//

static bool
impl_input(struct widget *                    widget,
           struct widget_control * const      control,
           struct surface_event const * const event)
{
  return true;
}

//
//
//

widget_fps_t
widget_fps_create(float glyph_width)
{
  widget_fps_t widget_fps = { .impl = malloc(sizeof(*widget_fps.impl)) };

  uint64_t const timestamp = impl_timestamp();

  // use a designated initializer
  *widget_fps.impl = (struct widget_fps)
  {
    .widget = {
      .pfn = {
        .destroy = impl_destroy,
        .layout  = impl_layout,
        .regen   = impl_regen,
        .input   = impl_input,
      },
    },

    .glyph_width = glyph_width,

    .timestamps = { timestamp, timestamp, /* don't care about [2] */ },

    .is_quiet = false,

    .is_group = false
  };

  return widget_fps;
}

//
//
//
