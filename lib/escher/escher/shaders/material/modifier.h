// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace escher {

// Specifies properties which affect the behavior of a material shader
// independently of the characteristics of the material itself, such as
// applying an analytic mask function to produce a shape.
class Modifier {
 public:
  enum class Mask { kNone = 0, kCircular = 1 };

  Modifier();
  ~Modifier();

  void set_mask(Mask mask) { mask_ = mask; }
  Mask mask() const { return mask_; }

 private:
  Mask mask_ = Mask::kNone;
};

}  // namespace escher
