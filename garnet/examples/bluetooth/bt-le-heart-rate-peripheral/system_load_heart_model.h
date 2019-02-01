// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_BLUETOOTH_BT_LE_HEART_RATE_PERIPHERAL_SYSTEM_LOAD_HEART_MODEL_H_
#define GARNET_EXAMPLES_BLUETOOTH_BT_LE_HEART_RATE_PERIPHERAL_SYSTEM_LOAD_HEART_MODEL_H_

#include "service.h"

#include <lib/zx/handle.h>
#include <lib/zx/time.h>

namespace bt_le_heart_rate {

// Example "heart" model whose "rate" is % CPU time busy and "energy expended"
// is number of context switches.
class SystemLoadHeartModel : public HeartModel {
 public:
  SystemLoadHeartModel();
  ~SystemLoadHeartModel() = default;

  // HeartModel implementation
  bool ReadMeasurement(Measurement* measurement) override;
  void ResetEnergyExpended() override { energy_counter_ = 0; }

 private:
  bool ReadCpuStats();

  zx::handle root_resource_;
  std::vector<zx_info_cpu_stats_t> cpu_stats_;
  std::vector<zx_info_cpu_stats_t> last_cpu_stats_;
  zx::time last_read_time_;
  uint64_t energy_counter_ = 0;
};

}  // namespace bt_le_heart_rate

#endif  // GARNET_EXAMPLES_BLUETOOTH_BT_LE_HEART_RATE_PERIPHERAL_SYSTEM_LOAD_HEART_MODEL_H_
