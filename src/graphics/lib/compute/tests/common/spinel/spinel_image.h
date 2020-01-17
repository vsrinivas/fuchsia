// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_SPINEL_IMAGE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_SPINEL_IMAGE_H_

#include <spinel/spinel_types.h>

#include <utility>

struct transform_stack;

// Convenience holder for several Spinel object handles/pointers related to
// a single rendered image (i.e. paths + rasters + composition + styling)
// for Spinel.
//
// Uses init() and reset() methods to be used with ScopedStruct<SpinelImage>
// as well.
//
// Usage:
//    1) Create instance, and call init() with or without a custom config.
//    2) Access the context, builders, composition and styling handles directly.
//    3) Allocated path and raster handles should be managed by the client code.
//    4) Optionally call render() to render the image.
//    5) Once done, reset() the instance to release all objects.
//
struct SpinelImage
{
  // Optional configuration struct when creating a new instance.
  struct Config
  {
    uint32_t clip[4]            = { 0, 0, 4096, 4096 };
    uint32_t max_layer_count    = 4096;
    uint32_t max_commands_count = 16384;
  };

  // Initialize instance.
  void
  init(spn_context_t context, const Config & config);

  // Initialize instance with default configuration.
  void
  init(spn_context_t context);

  // Reset/finalise instance.
  void
  reset();

  // Render image into a target buffer/image.
  // |submit_ext| is the spn_render_submit_t::ext extension pointer to use.
  // |width| and |height| are the dimensions of the target in pixels.
  void
  render(void * submit_ext, uint32_t width, uint32_t height);

  // Misc Spinel handles created automatically as a convenience.
  // For now, these are exposed directly. We could provide read-only
  // accessors and hide them in a protected section in the future.
  spn_context_t            context         = nullptr;
  struct transform_stack * transform_stack = nullptr;
  spn_path_builder_t       path_builder    = nullptr;
  spn_raster_builder_t     raster_builder  = nullptr;
  spn_composition_t        composition     = nullptr;
  spn_styling_t            styling         = nullptr;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_SPINEL_IMAGE_H_
