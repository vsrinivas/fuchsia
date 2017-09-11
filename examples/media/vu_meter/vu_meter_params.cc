// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/media/vu_meter/vu_meter_params.h"

#include "lib/fxl/strings/split_string.h"

namespace examples {

VuMeterParams::VuMeterParams(const fxl::CommandLine& command_line) {
  is_valid_ = true;
}

}  // namespace examples
