// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heart_model.h"

#include <zircon/syscalls.h>

namespace bt_le_heart_rate {

HeartModel::Measurement HeartModel::ReadMeasurement() {
  Measurement measurement;
  // Energy expended is a count of reads since the last reset.
  energy_expended_++;

  measurement.contact = true;

  uint8_t random[1];
  zx_cprng_draw(random, sizeof(random));
  measurement.rate = random[0];

  measurement.energy_expended = energy_expended_;

  return measurement;
}

void HeartModel::ResetEnergyExpended() { energy_expended_ = 0; }

}  // namespace bt_le_heart_rate
