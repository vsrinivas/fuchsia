// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "testclock.h"

namespace clk {

zx_status_t TestMuxClockTrivial::SetInput(const uint32_t index) {
  if (index >= n_) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  index_ = index;

  return ZX_OK;
}

zx_status_t TestMuxClockTrivial::GetNumInputs(uint32_t* out) {
  *out = n_;

  return ZX_OK;
}

zx_status_t TestMuxClockTrivial::GetInput(uint32_t* out) {
  *out = index_;

  return ZX_OK;
}

zx_status_t TestMuxClockTrivial::GetInputId(const uint32_t index, uint32_t* id) {
  if (index >= n_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  *id = parents_[index];
  return ZX_OK;
}

uint32_t TestMuxClockTrivial::ParentId() { return parents_[index_]; }

}  // namespace clk
