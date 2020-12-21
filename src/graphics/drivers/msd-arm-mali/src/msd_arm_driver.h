// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_ARM_DRIVER_H
#define MSD_ARM_DRIVER_H

#include <lib/inspect/cpp/inspect.h>

#include <memory>

#include "magma_util/macros.h"
#include "msd.h"
#include "msd_arm_device.h"

class MsdArmDriver : public msd_driver_t {
 public:
  virtual ~MsdArmDriver() {}

  static std::unique_ptr<MsdArmDriver> Create();
  static void Destroy(MsdArmDriver* drv);

  static MsdArmDriver* cast(msd_driver_t* drv) {
    DASSERT(drv);
    DASSERT(drv->magic_ == kMagic);
    return static_cast<MsdArmDriver*>(drv);
  }

  void configure(uint32_t flags) { configure_flags_ = flags; }

  uint32_t configure_flags() { return configure_flags_; }

  uint32_t DuplicateInspectHandle();

  inspect::Node& root_node() { return root_node_; }

  std::unique_ptr<MsdArmDevice> CreateDevice(void* device_handle);

  std::unique_ptr<MsdArmDevice> CreateDeviceForTesting(
      std::unique_ptr<magma::PlatformDevice> platform_device,
      std::unique_ptr<magma::PlatformBusMapper> bus_mapper);

 private:
  MsdArmDriver();

  static const uint32_t kMagic = 0x64726976;  //"driv"

  uint32_t configure_flags_ = 0;
  inspect::Inspector inspector_;
  // Available under the bootstrap/driver_manager:root/msd-arm-mali selector or
  // in /dev/diagnotics/class/gpu/000.inspect
  inspect::Node root_node_;
};

#endif  // MSD_ARM_DRIVER_H
