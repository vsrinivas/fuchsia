// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <vector>

#if USE_MOLD
#define DEMO_CLASS_HEADER "common/demo_app_mold.h"
#define DEMO_CLASS_NAME DemoAppMold
#define ONLY_IF_MOLD(...) __VA_ARGS__
#define ONLY_IF_SPINEL(...) /* nothing */
#else                       // !USE_MOLD
#define DEMO_CLASS_HEADER "common/demo_app_spinel.h"
#define DEMO_CLASS_NAME DemoAppSpinel
#define ONLY_IF_MOLD(...) /* nothing */
#define ONLY_IF_SPINEL(...) __VA_ARGS__
#endif  // !USE_MOLD

// This weird include is to work-around the fact that gn --check doesn't
// seem to properly understand conditional includes when they appear in
// #ifdef .. #endif blocks. Note that this is standard cpp behaviour!
#include DEMO_CLASS_HEADER

#include "common/demo_image.h"
#include "common/demo_utils.h"
#include "svg_scene_demo_image.h"
#include "tests/common/affine_transform.h"
#include "tests/common/argparse.h"
#include "tests/common/svg/scoped_svg.h"
#include "tests/common/svg/svg_bounds.h"
#include "tests/common/svg/svg_scene.h"
#include "tests/common/utils.h"

#define DEMO_SURFACE_WIDTH 1024
#define DEMO_SURFACE_HEIGHT 1024

#ifndef PROGRAM_NAME
#define PROGRAM_NAME "svg_ring_demo"
#endif

int
main(int argc, const char ** argv)
{
  // clang-format off

#define MY_OPTIONS_LIST(param)                                                                     \
  ARGPARSE_OPTION_INT(param, count, 'c', "count", "Number of SVG instances in ring.")              \
  ARGPARSE_OPTION_DOUBLE(param, radius, 'r', "radius", "Select inner ring radius in pixels.")      \
  ARGPARSE_OPTION_DOUBLE(param, scale, 's', "scale", "Apply scale to each SVG instance.")          \
  ARGPARSE_OPTION_DOUBLE(param, fixed_rotation, 'R', \
      "fixed-rotation", "Use a fixed rotation parameter. Value is in degrees.") \
  ARGPARSE_OPTION_FLAG(param, debug, 'D', "debug",                                                 \
      "Enable debug messages and Vulkan validation layers.")                                       \
  ARGPARSE_OPTION_STRING(param, window, '\0', "window", "Set window dimensions (e.g. 800x600).")   \
  ARGPARSE_OPTION_STRING(param, device, '\0', "device", "Device selection (vendor:device) IDs.")   \
  ARGPARSE_OPTION_STRING(param, format, '\0', "format", "Force pixel format [RGBA, BGRA].")        \
  ARGPARSE_OPTION_FLAG(param, fps, '\0', "fps", "Print frames per seconds to stdout.")             \
  ARGPARSE_OPTION_FLAG(param, no_vsync, '\0', "no-vsync",                                          \
      "Disable vsync synchronization. Useful for benchmarking. Note that this will disable "       \
      "presentation on Fuchsia as well.")                                                          \
  ARGPARSE_OPTION_FLAG(param, no_clear, '\0', "no-clear",                                          \
      "Disable image clear before rendering. Useful for benchmarking raw rendering performance.")

  // clang-format on
  ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS_LIST);
  if (!ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS_LIST))
    {
      if (options.help_needed)
        ARGPARSE_PRINT_HELP(PROGRAM_NAME, "A short demo of Spinel rendering", MY_OPTIONS_LIST);
      exit(options.help_needed ? EXIT_SUCCESS : EXIT_FAILURE);
    }

  uint32_t vendor_id, device_id;
  if (!parseDeviceOption(options.device, &vendor_id, &device_id))
    return EXIT_FAILURE;

  uint32_t window_width, window_height;
  if (!parseWindowOption(options.window,
                         DEMO_SURFACE_WIDTH,
                         DEMO_SURFACE_HEIGHT,
                         &window_width,
                         &window_height))
    return EXIT_FAILURE;

  uint32_t ring_size = 10;
  if (options.count.used)
    {
      ring_size = (options.count.value < 1) ? 0u : (uint32_t)options.count.value;
    }

  double ring_radius = 20.;
  if (options.radius.used)
    {
      ring_radius = options.radius.value;
    }

  double ring_scale = 1.0;
  if (options.scale.used)
    {
      ring_scale = options.scale.value;
    }

  // Parse the SVG input document.
  if (argc < 2)
    {
      fprintf(stderr, "This program requires an input svg file path!\n");
      return EXIT_FAILURE;
    }
  ScopedSvg svg = ScopedSvg::parseFile(argv[1]);
  ASSERT_MSG(svg.get(), "Could not parse input SVG file: %s\n", argv[1]);

  VkOffset2D image_center = {};

  double svg_xmin, svg_ymin, svg_xmax, svg_ymax;
  svg_estimate_bounds(svg.get(), nullptr, &svg_xmin, &svg_ymin, &svg_xmax, &svg_ymax);

  if (options.debug)
    {
      printf("Image bounds min=(%g,%g) max=(%g,%g) width=%g height=%g\n",
             svg_xmin,
             svg_ymin,
             svg_xmax,
             svg_ymax,
             svg_xmax - svg_xmin,
             svg_ymax - svg_ymin);
    }

  if (svg_xmin >= svg_xmax || svg_ymin >= svg_ymax)
    {
      fprintf(stderr, "WARNING: Could not compute bounds of SVG document!\n");
    }
  else
    {
      image_center.x = (int32_t)((svg_xmin + svg_xmax) / 2.);
      image_center.y = (int32_t)((svg_ymin + svg_ymax) / 2.);
    }

  // Create the application window.
  DEMO_CLASS_NAME::Config config = {
    .app = {
      .app_name      = PROGRAM_NAME,
      .window_width  = window_width,
      .window_height = window_height,
      .verbose       = options.debug,
      .debug         = options.debug,
      .disable_vsync = options.no_vsync,
      .print_fps     = options.fps,
    },
    .no_clear = options.no_clear,
  };

  DEMO_CLASS_NAME demo(config);

  VkExtent2D swapchain_extent = demo.window_extent();

  // Build ring as an SvgScene, centered around the window center.
  double win_center_x = swapchain_extent.width * 0.5;
  double win_center_y = swapchain_extent.height * 0.5;

  double rotation_center_x = (svg_xmin + svg_xmax) * 0.5;
  double rotation_center_y = svg_ymax + ring_radius;

  SvgScene svg_scene;
  for (uint32_t nn = 0; nn < ring_size; ++nn)
    {
      affine_transform_t transform =
        affine_transform_make_translation(-rotation_center_x, -rotation_center_y);

      if (ring_scale != 1.0)
        {
          transform =
            affine_transform_multiply_by_value(affine_transform_make_scale(ring_scale), transform);
        }

      transform = affine_transform_multiply_by_value(
        affine_transform_make_rotation(M_PI * 2 * nn / ring_size),
        transform);

      transform.tx += win_center_x;
      transform.ty += win_center_y;

      svg_scene.addSvgDocument(svg.get(), transform);
    }

  // Determine per-frame transform / animation.
  auto per_frame_transform = [options, win_center_x, win_center_y](uint32_t frame_counter) {
    double angle = options.fixed_rotation.used ? (options.fixed_rotation.value * M_PI / 180.)
                                               : (frame_counter / 60.) * M_PI;

    affine_transform_t affine =
      affine_transform_make_rotation_xy(angle, win_center_x, win_center_y);

    return spn_transform_t{
      .sx  = (float)affine.sx,
      .shx = (float)affine.shx,
      .tx  = (float)affine.tx,
      .shy = (float)affine.shy,
      .sy  = (float)affine.sy,
      .ty  = (float)affine.ty,
    };
  };

  demo.setImageFactory(SvgSceneDemoImage::makeFactory(svg_scene, per_frame_transform));

  demo.run();

  return EXIT_SUCCESS;
}
