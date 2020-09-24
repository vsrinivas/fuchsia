// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CLOCKTREE_CLOCKTREE_H_
#define CLOCKTREE_CLOCKTREE_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <stdint.h>
#include <zircon/types.h>

#include <fbl/mutex.h>
#include <fbl/span.h>

namespace clk {

class BaseClock;

// clk::Tree class manages an array of BaseClock objects that represent a clock tree
// For more information see ClockTree.md -- TODO(fxbug.dev/45916): Write documentation.
//
// Example Usage:
//   // Create some clock classes that inherit from BaseClock
//   class MyGateClock : public clk::BaseClock {};
//   class MyMuxClock : public clk::BaseClock {};
//
//   // Instantiate these classes.
//   MyGateClock gateClock;
//   MyMuxClock muxClock;
//
//   // Add these clocks to an array of clocks and instantiate a clk::Tree.
//   BaseClock* clocks[] = { &gateClock, &muxClock };
//   clk::Tree tree(clocks, countof(clocks));
//
//   // Now the Clock tree can be manupulated via calls to the Tree class as follows:
//   zx_status_t st = tree.Enable(kClk0);

class Tree {
 public:
  // Notes on ownership:
  //  (1) The caller is responsible for managing the `clocks` array's memory. The tree class will
  //      not attempt to free the `clocks` array Note that this permits the caller to statically
  //      allocate the clock array.
  //  (2) While the `Tree` class is in scope, it is not safe to manipulate the underlying clocks
  //      array. While the lifetime of the `clocks` array is the responsibility of the caller, the
  //      `Tree` class maintains exclusive access to the array for the duration of its lifespan. 
  explicit Tree(fbl::Span<BaseClock*> clocks, const uint32_t count)
      : clocks_(clocks), count_(count) {}

  zx_status_t Enable(const uint32_t id) TA_EXCL(topology_mutex_);
  zx_status_t Disable(const uint32_t id) TA_EXCL(topology_mutex_);
  zx_status_t IsEnabled(const uint32_t id, bool* out) TA_EXCL(topology_mutex_);

  zx_status_t SetRate(const uint32_t id, const Hertz rate) TA_EXCL(topology_mutex_);
  zx_status_t QuerySupportedRate(const uint32_t id, const Hertz max) TA_EXCL(topology_mutex_);
  zx_status_t GetRate(const uint32_t id, Hertz* out) TA_EXCL(topology_mutex_);

  zx_status_t SetInput(const uint32_t id, const uint32_t input_index) TA_EXCL(topology_mutex_);
  zx_status_t GetNumInputs(const uint32_t id, uint32_t* out) TA_EXCL(topology_mutex_);
  zx_status_t GetInput(const uint32_t id, uint32_t* out) TA_EXCL(topology_mutex_);

 private:
  // The following are locked variants of the public interface.
  zx_status_t EnableLocked(const uint32_t id) TA_REQ(topology_mutex_);
  zx_status_t DisableLocked(const uint32_t id) TA_REQ(topology_mutex_);
  zx_status_t IsEnabledLocked(const uint32_t id, bool* out) TA_REQ(topology_mutex_);

  zx_status_t SetRateLocked(const uint32_t id, const Hertz rate) TA_REQ(topology_mutex_);
  zx_status_t QuerySupportedRateLocked(const uint32_t id, const Hertz max) TA_REQ(topology_mutex_);
  zx_status_t GetRateLocked(const uint32_t id, Hertz* out) TA_REQ(topology_mutex_);

  zx_status_t SetInputLocked(const uint32_t id, const uint32_t input_index) TA_REQ(topology_mutex_);
  zx_status_t GetNumInputsLocked(const uint32_t id, uint32_t* out) TA_REQ(topology_mutex_);
  zx_status_t GetInputLocked(const uint32_t id, uint32_t* out) TA_REQ(topology_mutex_);

  bool InRange(const uint32_t index) const;

  fbl::Span<BaseClock*> clocks_ TA_GUARDED(topology_mutex_);
  const uint32_t count_;

  // Guards topology changes to the clock tree.
  fbl::Mutex topology_mutex_;
};

}  // namespace clk

#endif  // CLOCKTREE_CLOCKTREE_H_
