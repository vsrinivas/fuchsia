// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gamma-rgb-registers.h"

namespace camera {

void GammaRgbRegisters::WriteRegisters() {
  register_defs_.enable().ReadFrom(&mmio_local_).set_value(enable_).WriteTo(&mmio_local_);

  register_defs_.gain_rg()
      .ReadFrom(&mmio_local_)
      .set_gain_r(gain_r_)
      .set_gain_g(gain_g_)
      .WriteTo(&mmio_local_);

  register_defs_.gain_b().ReadFrom(&mmio_local_).set_value(gain_b_).WriteTo(&mmio_local_);

  register_defs_.offset_rg()
      .ReadFrom(&mmio_local_)
      .set_offset_r(offset_r_)
      .set_offset_g(offset_g_)
      .WriteTo(&mmio_local_);

  register_defs_.offset_b().ReadFrom(&mmio_local_).set_value(offset_b_).WriteTo(&mmio_local_);
}

}  // namespace camera
