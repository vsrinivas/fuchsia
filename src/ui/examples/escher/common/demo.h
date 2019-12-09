// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_ESCHER_COMMON_DEMO_H_
#define SRC_UI_EXAMPLES_ESCHER_COMMON_DEMO_H_

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/util/tracer.h"
#include "src/ui/lib/escher/vk/vulkan_context.h"

// Base class for Escher demos.
class Demo {
 public:
  // Max number of simultaneous frames in flight.  Based on caching behaviour built into Escher.
  static constexpr uint32_t kMaxOutstandingFrames = 3;

  explicit Demo(escher::EscherWeakPtr escher, const char* name);
  virtual ~Demo();

  // |key| must contain either a single alpha-numeric character (uppercase
  // only), or one of the special values "ESCAPE", "SPACE", and "RETURN".
  // Return true if the key-press was handled, and false otherwise.
  virtual bool HandleKeyPress(std::string key);

  // Begin a touch.  Only one touch can have the specified |touch_id| at any
  // given time (it is guaranteed to not be reused until EndTouch() is called).
  virtual void BeginTouch(uint64_t touch_id, double x_position, double y_position) {}
  // Continue a touch.  Multiple positions may be aggregated since the last time
  // that BeginTouch()/ContinueTouch() were called; the number of positions is
  // provided by |position_count| which is guaranteed to be >= 1.  If multiple
  // positions are aggregated, they are provided in order of occurrence.
  virtual void ContinueTouch(uint64_t touch_id, const double* x_positions,
                             const double* y_positions, size_t position_count) {}
  // End a touch.  Afterward, |touch_id| may appear in a subsequent call of
  // BeginTouch().
  virtual void EndTouch(uint64_t touch_id, double x_position, double y_position) {}

  virtual void DrawFrame(const escher::FramePtr& frame, const escher::ImagePtr& output_image,
                         const escher::SemaphorePtr& framebuffer_acquired) = 0;

  const char* name() const { return name_; }
  escher::Escher* escher() { return escher_.get(); }
  escher::EscherWeakPtr GetEscherWeakPtr() { return escher_->GetWeakPtr(); }
  const escher::VulkanContext& vulkan_context() const { return vulkan_context_; }

  // Helper function that draws the specified number of frames to an offscreen buffer.
  static void RunOffscreenBenchmark(Demo* demo, uint32_t framebuffer_width,
                                    uint32_t framebuffer_height, vk::Format framebuffer_format,
                                    size_t frame_count);

 protected:
  void ToggleTracing();

 private:
  const char* name_;
  escher::EscherWeakPtr escher_;
  escher::VulkanContext vulkan_context_;
  std::unique_ptr<escher::Tracer> tracer_;
};

#endif  // SRC_UI_EXAMPLES_ESCHER_COMMON_DEMO_H_
