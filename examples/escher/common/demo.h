// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_ESCHER_COMMON_DEMO_H_
#define GARNET_EXAMPLES_ESCHER_COMMON_DEMO_H_

#include "garnet/examples/escher/common/demo_harness.h"
#include "lib/escher/escher.h"
#include "lib/escher/util/tracer.h"
#include "lib/escher/vk/vulkan_context.h"

// Base class for Escher demos.
class Demo {
 public:
  explicit Demo(DemoHarness* harness);
  virtual ~Demo();

  // |key| must contain either a single alpha-numeric character (uppercase
  // only), or one of the special values "ESCAPE", "SPACE", and "RETURN".
  // Return true if the key-press was handled, and false otherwise.
  virtual bool HandleKeyPress(std::string key);

  // Begin a touch.  Only one touch can have the specified |touch_id| at any
  // given time (it is guaranteed to not be reused until EndTouch() is called).
  virtual void BeginTouch(uint64_t touch_id, double x_position,
                          double y_position) {}
  // Continue a touch.  Multiple positions may be aggregated since the last time
  // that BeginTouch()/ContinueTouch() were called; the number of positions is
  // provided by |position_count| which is guaranteed to be >= 1.  If multiple
  // positions are aggregated, they are provided in order of occurrence.
  virtual void ContinueTouch(uint64_t touch_id, const double* x_positions,
                             const double* y_positions, size_t position_count) {
  }
  // End a touch.  Afterward, |touch_id| may appear in a subsequent call of
  // BeginTouch().
  virtual void EndTouch(uint64_t touch_id, double x_position,
                        double y_position) {}

  virtual void DrawFrame() = 0;

  DemoHarness* harness() const { return harness_; }
  escher::Escher* escher() { return &escher_; }
  const escher::VulkanContext& vulkan_context() const {
    return vulkan_context_;
  }
  escher::EscherWeakPtr GetEscherWeakPtr() { return escher_.GetWeakPtr(); }

 protected:
  void ToggleTracing();

 private:
  DemoHarness* const harness_;
  escher::VulkanContext vulkan_context_;
  escher::Escher escher_;
  std::unique_ptr<escher::Tracer> tracer_;
};

#endif  // GARNET_EXAMPLES_ESCHER_COMMON_DEMO_H_
