// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "frame-pointer.h"

#include "stack.h"

FramePointer& FramePointer::operator++() {
  *this = IsOnStack(reinterpret_cast<uintptr_t>(fp_)) ? *fp_ : end();
  return *this;
}
