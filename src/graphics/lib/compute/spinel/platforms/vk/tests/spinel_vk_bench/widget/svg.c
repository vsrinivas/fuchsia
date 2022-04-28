// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "widget/svg.h"

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/macros.h"
#include "spinel/ext/svg2spinel/svg2spinel.h"
#include "spinel/spinel_assert.h"
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

float
impl_vec2_norm(struct impl_vec2 const * const a)
{
  return sqrtf(a->x * a->x + a->y * a->y);
}

float
impl_vec2_dot(struct impl_vec2 const * const a, struct impl_vec2 const * const b)
{
  return a->x * b->x + a->y * b->y;
}

float
impl_vec2_cross(struct impl_vec2 const * const a, struct impl_vec2 const * const b)
{
  return a->x * b->y - a->y * b->x;
}

//
//
//

#define IMPL_INPUT_TRANSFORM_SCALE_FACTOR 1.05f

struct impl_input_xform
{
  struct impl_vec2 origin;
  struct impl_vec2 center;

  float rotate;
  float scale;

  struct
  {
    struct impl_vec2 v;  // vector
    float            n;  // norm
  } pinch_zoom;
};

//
//
//

struct impl_input
{
  struct impl_input_xform xform;
  bool                    is_control;
};

//
//
//

struct widget_svg
{
  struct widget     widget;
  struct svg *      svg;
  struct impl_input input;
  spinel_path_t *   paths;
  spinel_raster_t * rasters;
  bool              is_srgb;

  //
  // FIXME(allanmac): Eventually decide whether or not the svg always
  // (or never) creates its own styling group.
  //
  bool is_group;
};

//
//
//

static void
impl_paths_release(struct widget_svg * impl, struct widget_context * const context)
{
  if (impl->paths != NULL)
    {
      spinel_svg_paths_release(impl->svg, context->context, impl->paths);

      impl->paths = NULL;
    }
}

static void
impl_rasters_release(struct widget_svg * impl, struct widget_context * const context)
{
  if (impl->rasters != NULL)
    {
      spinel_svg_rasters_release(impl->svg, context->context, impl->rasters);

      impl->rasters = NULL;
    }
}

//
//
//

static void
impl_destroy(struct widget * widget, struct widget_context * const context)
{
  widget_svg_t svg = { .widget = widget };

  impl_paths_release(svg.impl, context);

  impl_rasters_release(svg.impl, context);

  free(svg.impl);
}

//
//
//

static void
impl_layout(struct widget *              widget,
            struct widget_layout * const layout,
            uint32_t * const             group_depth_max)
{
  widget_svg_t svg = { .widget = widget };

  //
  // NOTE(allanmac): There is no advantage right now to
  // representing the svg with its own child group.
  //
  widget_simple_impl_layout(widget,
                            layout,
                            group_depth_max,
                            svg.impl->is_group,
                            svg_layer_count(svg.impl->svg));
}

//
//
//

static void
impl_regen(struct widget *                     widget,
           struct widget_control const * const control,
           struct widget_context * const       context)
{
  widget_svg_t svg = { .widget = widget };

  //
  // regen paths?
  //
  if (control->paths)
    {
      // release existing
      impl_paths_release(svg.impl, context);

      // create new
      svg.impl->paths = spinel_svg_paths_decode(svg.impl->svg, context->pb);
    }

  //
  // regen rasters?
  //
  // FIXME(allanmac): raster translation isn't available yet
  //
  if (control->rasters)
    {
      assert(svg.impl->paths != NULL);

      // release existing
      impl_rasters_release(svg.impl, context);

      // update transform stack
      uint32_t const ts_save = spinel_transform_stack_save(context->ts);

      spinel_transform_stack_push_rotate_scale_xy(context->ts,
                                                  svg.impl->input.xform.rotate,
                                                  svg.impl->input.xform.scale,
                                                  svg.impl->input.xform.scale,
                                                  svg.impl->input.xform.center.x,
                                                  svg.impl->input.xform.center.y);
      spinel_transform_stack_concat(context->ts);

      spinel_transform_stack_push_translate(context->ts,
                                            svg.impl->input.xform.origin.x,
                                            svg.impl->input.xform.origin.y);
      spinel_transform_stack_concat(context->ts);

      // define rasters
      svg.impl->rasters = spinel_svg_rasters_decode(svg.impl->svg,  //
                                                    context->rb,
                                                    svg.impl->paths,
                                                    context->ts);
      // restore transform stack
      spinel_transform_stack_restore(context->ts, ts_save);
    }

  //
  // regen styling and composition?
  //
  if (control->styling && control->composition)
    {
      assert(svg.impl->rasters != NULL);

      spinel_group_id group_id;

      widget_simple_impl_styling_group(svg.widget, control, context, svg.impl->is_group, &group_id);

      //
      // decode the svg styling and composition
      //
      spinel_svg_layers_decode_at(svg.widget->layout.group.layer.base,
                                  group_id,
                                  svg.impl->svg,
                                  svg.impl->rasters,
                                  context->composition.curr,
                                  context->styling.curr,
                                  svg.impl->is_srgb);
    }
}

//
//
//

static void
impl_input_point_set_center(struct impl_input * input, float const x, float const y)
{
  // device-space vector from old center to new center
  struct impl_vec2 const c = { x - input->xform.center.x, y - input->xform.center.y };

  // undo scale
  struct impl_vec2 const c_s = { c.x / input->xform.scale, c.y / input->xform.scale };

  // undo rotate
  float const untheta   = -input->xform.rotate;
  float const cos_theta = cosf(untheta);  // replace with cospi if available
  float const sin_theta = sinf(untheta);  // replace with sinpi if available

  struct impl_vec2 const c_rs = { c_s.x * cos_theta - c_s.y * sin_theta,
                                  c_s.x * sin_theta + c_s.y * cos_theta };

  // adjust object-space center
  input->xform.center.x += c_rs.x;
  input->xform.center.y += c_rs.y;

  // shift center and origin so it remains stationary
  struct impl_vec2 const d = { x - input->xform.center.x, y - input->xform.center.y };

  // shift frame of new center
  input->xform.center.x += d.x;
  input->xform.center.y += d.y;

  // shift frame of origin
  input->xform.origin.x += d.x;
  input->xform.origin.y += d.y;
}

//
//
//

static void
impl_input_point_init_pinch_zoom(struct impl_input * input,  //
                                 float const         x0,
                                 float const         y0,
                                 float const         x1,
                                 float const         y1)
{
  struct impl_vec2 const v = { x1 - x0, y1 - y0 };
  float const            n = impl_vec2_norm(&v);

  input->xform.pinch_zoom.v = v;
  input->xform.pinch_zoom.n = n;
}

static bool
impl_input_point_set_pinch_zoom(struct impl_input * input,  //
                                float const         x0,
                                float const         y0,
                                float const         x1,
                                float const         y1)
{
  bool is_rerasterize = false;

  struct impl_vec2 const v1 = { x1 - x0, y1 - y0 };
  float const            n1 = impl_vec2_norm(&v1);

  // scale the scale
  float const scale = n1 / input->xform.pinch_zoom.n;

  if (scale != 1.0f)
    {
      input->xform.scale *= scale;

      is_rerasterize = true;
    }

  //
  // See W. Kahan "Computing Cross-Products and Rotations in 2- and
  // 3-Dimensional Euclidean Spaces" and a number of his other papers
  // for deep discussions on computing the angle between vectors.
  //
  float const theta = atan2f(impl_vec2_cross(&input->xform.pinch_zoom.v, &v1),
                             impl_vec2_dot(&input->xform.pinch_zoom.v, &v1));

  // update v0 with v1
  input->xform.pinch_zoom.v = v1;
  input->xform.pinch_zoom.n = n1;

  if (theta != 0.0f)
    {
      input->xform.rotate = fmodf(input->xform.rotate + theta, ((float)M_PI * 2.0));
      is_rerasterize      = true;
    }

  return is_rerasterize;
}

//
// NOTE(allanmac): For now, moving the svg forces local regen of the
// rasters and global regen of the styling and composition.  This will
// change when the composition and styling are incrementally updatable.
//

static void
impl_rerasterize(widget_svg_t * const svg, struct widget_control * const control)
{
  control->rasters     = 1;
  control->styling     = 1;
  control->composition = 1;
  control->render      = 1;
}

//
//
//

void
widget_svg_center(widget_svg_t            svg,  //
                  struct widget_control * control,
                  VkExtent2D const *      extent,
                  float                   cx,
                  float                   cy,
                  float                   scale)
{
  float const extent_cx = (float)(extent->width / 2);
  float const extent_cy = (float)(extent->height / 2);

  svg.impl->input.xform.center.x = extent_cx;
  svg.impl->input.xform.center.y = extent_cy;

  svg.impl->input.xform.origin.x = extent_cx - cx;
  svg.impl->input.xform.origin.y = extent_cy - cy;

  svg.impl->input.xform.scale = scale;

  impl_rerasterize(&svg, control);
}

//
//
//

void
widget_svg_rotate(widget_svg_t svg, struct widget_control * control, float theta)
{
  if (svg.impl->input.xform.rotate != theta)
    {
      svg.impl->input.xform.rotate = fmodf(theta, (float)(M_PI * 2.0));

      impl_rerasterize(&svg, control);
    }
}

//
//
//

static bool
impl_input(struct widget *                    widget,
           struct widget_control * const      control,
           struct surface_event const * const event)
{
  widget_svg_t svg = { .widget = widget };

  //
  // NOTE: the current SVG decoder requires an unsealed styling and
  // composition so if one is enabled then enable the other
  //
  if (control->styling || control->composition)
    {
      control->styling     = 1;
      control->composition = 1;
    }

  //
  // process event
  //
  switch (event->type)
    {
        case SURFACE_EVENT_TYPE_KEYBOARD_PRESS: {
          switch (event->keyboard.code)
            {
              case SURFACE_KEY_S:
                svg.impl->is_srgb = !svg.impl->is_srgb;
                impl_rerasterize(&svg, control);
                fprintf(stdout,
                        "widget/svg.c.%s: %s\n",
                        __func__,
                        svg.impl->is_srgb                                 //
                          ? "SVG colors are sRGB and will be linearized"  //
                          : "SVG colors will not be linearized");
                break;

              case SURFACE_KEY_RIGHT:
                if (svg.impl->input.is_control)
                  {
                    svg.impl->input.xform.rotate += (float)(M_PI / 180.0);
                    svg.impl->input.xform.rotate = fmodf(svg.impl->input.xform.rotate,  //
                                                         (float)(M_PI * 2.0));
                  }
                else
                  {
                    svg.impl->input.xform.center.x += 1;
                    svg.impl->input.xform.origin.x += 1;
                  }
                impl_rerasterize(&svg, control);
                break;

              case SURFACE_KEY_LEFT:
                if (svg.impl->input.is_control)
                  {
                    svg.impl->input.xform.rotate -= (float)(M_PI / 180.0);
                    svg.impl->input.xform.rotate =
                      fmodf(svg.impl->input.xform.rotate, (float)(M_PI * 2.0));
                  }
                else
                  {
                    svg.impl->input.xform.center.x -= 1;
                    svg.impl->input.xform.origin.x -= 1;
                  }
                impl_rerasterize(&svg, control);
                break;

              case SURFACE_KEY_DOWN:
                if (svg.impl->input.is_control)
                  {
                    svg.impl->input.xform.scale /= IMPL_INPUT_TRANSFORM_SCALE_FACTOR;
                  }
                else
                  {
                    svg.impl->input.xform.center.y += 1;
                    svg.impl->input.xform.origin.y += 1;
                  }
                impl_rerasterize(&svg, control);
                break;

              case SURFACE_KEY_UP:
                if (svg.impl->input.is_control)
                  {
                    svg.impl->input.xform.scale *= IMPL_INPUT_TRANSFORM_SCALE_FACTOR;
                  }
                else
                  {
                    svg.impl->input.xform.center.y -= 1;
                    svg.impl->input.xform.origin.y -= 1;
                  }
                impl_rerasterize(&svg, control);
                break;

              case SURFACE_KEY_LEFT_CTRL:
              case SURFACE_KEY_RIGHT_CTRL:
                svg.impl->input.is_control = true;
                break;

              case SURFACE_KEY_EQUALS:
                // reset all input state
                svg.impl->input.xform = (struct impl_input_xform){ .scale = 1.0f };
                impl_rerasterize(&svg, control);
                break;

              default:
                break;
            }
          break;
        }

        case SURFACE_EVENT_TYPE_KEYBOARD_RELEASE: {
          switch (event->keyboard.code)
            {
              case SURFACE_KEY_LEFT_CTRL:
              case SURFACE_KEY_RIGHT_CTRL:
                svg.impl->input.is_control = false;
                break;

              default:
                break;
            }
          break;
        }

        case SURFACE_EVENT_TYPE_POINTER_INPUT: {
          if (event->pointer.buttons.button_1)
            {
              float const x = (float)event->pointer.x;
              float const y = (float)event->pointer.y;

              struct impl_vec2 const d = { x - svg.impl->input.xform.center.x,
                                           y - svg.impl->input.xform.center.y };

              if ((d.x != 0.0f) || (d.y != 0.0f))
                {
                  svg.impl->input.xform.center.x = x;
                  svg.impl->input.xform.center.y = y;

                  svg.impl->input.xform.origin.x += d.x;
                  svg.impl->input.xform.origin.y += d.y;

                  impl_rerasterize(&svg, control);
                }
            }
          break;
        }

        case SURFACE_EVENT_TYPE_POINTER_INPUT_SCROLL_V: {
          if (svg.impl->input.is_control)
            {
              svg.impl->input.xform.rotate += (float)event->pointer.v * (float)(M_PI / 180.0);
              svg.impl->input.xform.rotate = fmodf(svg.impl->input.xform.rotate,  //
                                                   (float)(M_PI * 2.0));
            }
          else
            {
              if (event->pointer.v > 0)
                {
                  svg.impl->input.xform.scale *= IMPL_INPUT_TRANSFORM_SCALE_FACTOR;
                }
              else
                {
                  svg.impl->input.xform.scale /= IMPL_INPUT_TRANSFORM_SCALE_FACTOR;
                }
            }
          impl_rerasterize(&svg, control);
          break;
        }

        case SURFACE_EVENT_TYPE_POINTER_INPUT_BUTTON_PRESS: {
          if (event->pointer.buttons.button_1)
            {
              impl_input_point_set_center(&svg.impl->input,
                                          (float)event->pointer.x,
                                          (float)event->pointer.y);
            }
          break;
        }

        case SURFACE_EVENT_TYPE_TOUCH_INPUT: {
          if ((event->touch.contact_count.prev == 1) && (event->touch.contact_count.curr == 1))
            {
              float const x =
                (float)((event->touch.extent.width *
                         (event->touch.contacts[0].x - event->touch.contact_axes.x.min)) /
                        (event->touch.contact_axes.x.max - event->touch.contact_axes.x.min));

              float const y =
                (float)((event->touch.extent.height *
                         (event->touch.contacts[0].y - event->touch.contact_axes.y.min)) /
                        (event->touch.contact_axes.y.max - event->touch.contact_axes.y.min));

              struct impl_vec2 const d = { x - svg.impl->input.xform.center.x,
                                           y - svg.impl->input.xform.center.y };

              if ((d.x != 0.0f) || (d.y != 0.0f))
                {
                  svg.impl->input.xform.center.x = x;
                  svg.impl->input.xform.center.y = y;

                  svg.impl->input.xform.origin.x += d.x;
                  svg.impl->input.xform.origin.y += d.y;

                  impl_rerasterize(&svg, control);
                }
            }
          else if ((event->touch.contact_count.prev == 2) && (event->touch.contact_count.curr == 2))
            {
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

              struct impl_vec2 const c = { (x0 + x1) * 0.5f, (y0 + y1) * 0.5f };
              struct impl_vec2 const d = { c.x - svg.impl->input.xform.center.x,
                                           c.y - svg.impl->input.xform.center.y };

              if ((d.x != 0.0f) || (d.y != 0.0f))
                {
                  svg.impl->input.xform.center.x = c.x;
                  svg.impl->input.xform.center.y = c.y;

                  svg.impl->input.xform.origin.x += d.x;
                  svg.impl->input.xform.origin.y += d.y;

                  impl_rerasterize(&svg, control);
                }

              if (impl_input_point_set_pinch_zoom(&svg.impl->input, x0, y0, x1, y1))
                {
                  impl_rerasterize(&svg, control);
                }
            }
          break;
        }

        case SURFACE_EVENT_TYPE_TOUCH_INPUT_CONTACT_COUNT: {
          if ((event->touch.contact_count.curr == 1) &&
              ((event->touch.contact_count.prev == 0) || (event->touch.contact_count.prev == 2)))
            {
              float const x =
                (float)((event->touch.extent.width *
                         (event->touch.contacts[0].x - event->touch.contact_axes.x.min)) /
                        (event->touch.contact_axes.x.max - event->touch.contact_axes.x.min));

              float const y =
                (float)((event->touch.extent.height *
                         (event->touch.contacts[0].y - event->touch.contact_axes.y.min)) /
                        (event->touch.contact_axes.y.max - event->touch.contact_axes.y.min));

              impl_input_point_set_center(&svg.impl->input, x, y);
            }
          else if ((event->touch.contact_count.prev <= 1) && (event->touch.contact_count.curr == 2))
            {
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

              impl_input_point_set_center(&svg.impl->input, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f);

              impl_input_point_init_pinch_zoom(&svg.impl->input, x0, y0, x1, y1);
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

widget_svg_t
widget_svg_create(struct svg * const svg, bool is_srgb)
{
  widget_svg_t widget_svg = { .impl = malloc(sizeof(*widget_svg.impl)) };

  // use a designated initializer
  *widget_svg.impl = (struct widget_svg){

    .widget = {
      .pfn = {
        .destroy = impl_destroy,
        .layout  = impl_layout,
        .regen   = impl_regen,
        .input   = impl_input,
      },
    },

    .svg = svg,

    .input = {
      .xform = {
        .scale = 1.0f,
      }
    },

    .is_srgb = is_srgb,

    .is_group = false
  };

  return widget_svg;
}

//
//
//
