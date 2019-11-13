// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SKIA_SKIA_TEST_APP_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SKIA_SKIA_TEST_APP_H_

#include <stdint.h>

#ifndef __cplusplus
#error "This header can only be included from C++ sources!"
#endif

class SkCanvas;
class SkiaTestAppImpl;

// Helper C++ class to define a test Skia application that opens a window
// and renders frames in a loop. Clients should derive drawFrame().
//
// This handles complex issues related to Skia <-> swapchain interactions
// automatically.
//
class SkiaTestApp {
 public:
  struct Config
  {
    const char * app_name;
    uint32_t     window_width;
    uint32_t     window_height;
    bool         enable_debug;
    bool         disable_vsync;
  };

  explicit SkiaTestApp(const Config & config);

  // Deprecated constructor!
  SkiaTestApp(const char * app_name, bool is_debug, uint32_t window_width, uint32_t window_height)
      : SkiaTestApp(Config{ app_name, window_width, window_height, is_debug, false })
  {
  }

  virtual ~SkiaTestApp();

  // Draw a single frame using skia.

  // |canvas| is a Skia canvas for the current swapchain image.
  virtual void
  drawFrame(SkCanvas * canvas, uint32_t frame_counter) = 0;

  // Run the application until it exits.
  void
  run();

 private:
  // All implementation details hidden from the client.
  SkiaTestAppImpl * impl_;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SKIA_SKIA_TEST_APP_H_
