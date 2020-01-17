// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_SPINEL_IMAGE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_SPINEL_IMAGE_H_

#include <stdint.h>

#include "spinel/spinel_types.h"

// Multiple demo programs are implemented that display one SpinelImage instance
// per frame, using either Spinel, Mold (or even Skia?) as a rendering backend.

// Said images can be built in different ways (e.g. by direct spinel calls,
// by parsing an SVG document) or simply by providing different animations
// transforms based on the frame counter.
//
// The two classes below are used to abstract the image's content from how
// it will be displayed exactly:
//
//     DemoSpinelImage is an abstract interface used by the demo programs
//     to setup each image's Spinel paths, rasters and layers
//     (i.e. composition + styling) before rendering.
//
//     DemoSpinelImageProvider is an abtract interface for a container
//     of DemoSpinelImage instances. The container must be able to implement
//     one separate instance per swapchain image.
//

// Abstract interface for a SpinelImage that the demo programs will use.
class DemoSpinelImage {
 public:
  DemoSpinelImage()          = default;
  virtual ~DemoSpinelImage() = default;

  virtual void
  setupPaths(uint32_t frame_counter) = 0;
  virtual void
  setupRasters(uint32_t frame_counter) = 0;
  virtual void
  setupLayers(uint32_t frame_counter) = 0;

  virtual void
  resetPaths() = 0;
  virtual void
  resetRasters() = 0;
  virtual void
  resetLayers() = 0;

  virtual void
  render(void * submit_ext, uint32_t clip_width, uint32_t clip_height) = 0;
};

// An abstract interface used by demo programs to get SpinelImage instances to
// render.
class DemoSpinelImageProvider {
 public:
  DemoSpinelImageProvider()          = default;
  virtual ~DemoSpinelImageProvider() = default;

  // Prepare |image_count| images for rendering.
  virtual void
  setup(spn_context_t context,
        uint32_t      image_count,
        uint32_t      surface_width,
        uint32_t      surface_height) = 0;

  // Return the n-th image to be rendered for frame identified by
  // |image_index|. This allows the provider to manage animations.
  virtual DemoSpinelImage &
  getImage(uint32_t image_index) = 0;

  // Dispose of all resources.
  virtual void
  teardown()
  {
  }
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_SPINEL_IMAGE_H_
