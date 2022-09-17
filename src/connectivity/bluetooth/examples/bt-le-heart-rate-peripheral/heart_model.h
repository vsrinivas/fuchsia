// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_LE_HEART_RATE_PERIPHERAL_HEART_MODEL_H_
#define SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_LE_HEART_RATE_PERIPHERAL_HEART_MODEL_H_

#include "service.h"

namespace bt_le_heart_rate {

class HeartModel {
 public:
  struct Measurement {
    bool contact;         // True if measured while sensor was in contact.
    int rate;             // Heart rate in beats per minute (BPM).
    int energy_expended;  // Energy expended since reset in kilojoules (kJ).
  };

  Measurement ReadMeasurement();

  void ResetEnergyExpended();

 private:
  int energy_expended_ = 0;
};

}  // namespace bt_le_heart_rate

#endif  // SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_LE_HEART_RATE_PERIPHERAL_HEART_MODEL_H_
