// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CLOCKTREE_BASECLOCK_H_
#define CLOCKTREE_BASECLOCK_H_

#include <stdint.h>
#include <zircon/types.h>

#include "types.h"

namespace clk {

// BaseClock is a base class that clock implementers should inherit from to implement
// various clock types.
// The Tree class accepts an array of BaseClock*s which represent a clock tree.
//
// See `clocktree-test.cc` for example usage.
class BaseClock {
 public:
  BaseClock(const char* name, const uint32_t id) : name_(name), id_(id), enable_count_(0) {}
  virtual ~BaseClock() {}
  /// Clock Gating Control.
  virtual zx_status_t Enable() = 0;
  virtual zx_status_t Disable() = 0;
  virtual zx_status_t IsEnabled(bool* out) = 0;

  // Clock frequency control.
  virtual zx_status_t SetRate(const Hertz rate, const Hertz parent_rate) = 0;
  virtual zx_status_t QuerySupportedRate(const Hertz max, const Hertz parent_rate, Hertz* out) = 0;
  virtual zx_status_t GetRate(const Hertz parent_rate, Hertz* out) = 0;

  // Clock Mux Control.
  virtual zx_status_t SetInput(const uint32_t index) = 0;
  virtual zx_status_t GetNumInputs(uint32_t* out) = 0;
  virtual zx_status_t GetInput(uint32_t* out) = 0;
  virtual zx_status_t GetInputId(const uint32_t index, uint32_t* id) = 0;

  // Accessors.
  const char* Name() const { return name_; }
  uint32_t Id() const { return id_; }
  virtual uint32_t ParentId() = 0;
  uint32_t EnableCount() { return enable_count_; }
  void SetEnableCount(uint32_t enable_count) { enable_count_ = enable_count; }

 private:
  const char* name_;
  const uint32_t id_;
  uint32_t enable_count_;
};

}  // namespace clk

#endif  // CLOCKTREE_BASECLOCK_H_
