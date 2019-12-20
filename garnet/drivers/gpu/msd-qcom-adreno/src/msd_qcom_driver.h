// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_QCOM_DRIVER_H
#define MSD_QCOM_DRIVER_H

#include <msd.h>

#include <magma_util/macros.h>

class MsdQcomDriver : public msd_driver_t {
 public:
  MsdQcomDriver() { magic_ = kMagic; }

  static MsdQcomDriver* cast(msd_driver_t* drv) {
    DASSERT(drv);
    DASSERT(drv->magic_ == kMagic);
    return static_cast<MsdQcomDriver*>(drv);
  }

 private:
  static const uint32_t kMagic = 0x64726976;  //"driv"
};

#endif  // MSD_QCOM_DRIVER_H
