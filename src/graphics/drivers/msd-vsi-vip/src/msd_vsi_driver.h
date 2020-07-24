// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_VSI_DRIVER_H
#define MSD_VSI_DRIVER_H

#include "magma_util/macros.h"
#include "msd.h"

class MsdVsiDriver : public msd_driver_t {
 public:
  MsdVsiDriver() { magic_ = kMagic; }

  virtual ~MsdVsiDriver() {}

  static MsdVsiDriver* cast(msd_driver_t* drv) {
    DASSERT(drv);
    DASSERT(drv->magic_ == kMagic);
    return static_cast<MsdVsiDriver*>(drv);
  }

 private:
  static const uint32_t kMagic = 0x64726976;  //"driv"
};

#endif  // MSD_VSI_DRIVER_H
