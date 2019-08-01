// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_ISP_MODULES_STATS_MGR_H_
#define SRC_CAMERA_DRIVERS_ISP_MODULES_STATS_MGR_H_

#include <lib/mmio/mmio.h>

#include <fbl/unique_ptr.h>

#include "sensor.h"

namespace camera {

// Takes the place of the fsm_mgr.
// Processes an event queue, and maintains ownership of all the modules.
// This class will be broken out into multiple classes based on utility, but
// this will serve as the initial step in porting functionality from the fsm
// architecture.
// Collects statistics from all the modules.
class StatsManager {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(StatsManager);
  explicit StatsManager(fbl::unique_ptr<camera::Sensor> sensor) : sensor_(std::move(sensor)) {}

  static fbl::unique_ptr<StatsManager> Create(ddk::MmioView isp_mmio, ddk::MmioView isp_mmio_local,
                                              ddk::CameraSensorProtocolClient camera_sensor);
  ~StatsManager();

  void SensorStartStreaming() { sensor_->StartStreaming(); }
  void SensorStopStreaming() { sensor_->StopStreaming(); }

 private:
  fbl::unique_ptr<camera::Sensor> sensor_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_ISP_MODULES_STATS_MGR_H_
