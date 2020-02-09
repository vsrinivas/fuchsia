// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_ARM_DRIVER_H
#define MSD_ARM_DRIVER_H

#include <memory>

#include "magma_util/macros.h"
#include "msd.h"

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

 private:
  MsdArmDriver();

  static const uint32_t kMagic = 0x64726976;  //"driv"

  uint32_t configure_flags_ = 0;
};

#endif  // MSD_ARM_DRIVER_H
