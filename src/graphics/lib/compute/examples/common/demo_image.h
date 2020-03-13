// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_IMAGE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_IMAGE_H_

#include <functional>
#include <memory>

#include "spinel/spinel_types.h"

// Multiple demo programs are implemented that display one SpinelImage instance
// per frame, using either Spinel, Mold (or even Skia?) as a rendering backend.
//
// This is an abstract interface to said images that will be used by the
// demo program. There will be typically one instance per swapchain image.
//
// See DemoImageGroup for an object that manages a collection of DemoImage
// instances.
//
class DemoImage {
 public:
  DemoImage()          = default;
  virtual ~DemoImage() = default;

  // Prepare image to render the n-th frame identified by |frame_counter|.
  // NOTE: Always followed by a render() call.
  virtual void
  setup(uint32_t frame_counter) = 0;

  // Render the prepared image. |submit_ext| is an spn_render_submit_t extension
  // pointer. |clip_width| and |clip_height| are the clipping dimensions for this
  // render.
  //
  // NOTE: Always followed by a flush() call.
  virtual void
  render(void * submit_ext, uint32_t clip_width, uint32_t clip_height) = 0;

  // Ensure the rendered image is properly flushed to the swapchain.
  // (e.g. unseal the Spinel composition/styling).
  //
  // NOTE: Always called after a render() call. May be called before any
  // setup() call though.
  virtual void
  flush() = 0;

  // Configuration parameters when creating a new DemoImage instance.
  struct Config
  {
    spn_context_t context;
    uint32_t      surface_width;
    uint32_t      surface_height;
    uint32_t      image_count;
  };

  // A callable object that creates a new DemoImage instance from a given
  // Config instance. Will be called |config.image_count| times at
  // application startup.
  using Factory = std::function<std::unique_ptr<DemoImage>(const Config & config)>;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_IMAGE_H_
