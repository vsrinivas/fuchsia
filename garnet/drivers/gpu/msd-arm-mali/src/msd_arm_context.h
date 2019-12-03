// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_GPU_MSD_ARM_MALI_SRC_MSD_ARM_CONTEXT_H_
#define GARNET_DRIVERS_GPU_MSD_ARM_MALI_SRC_MSD_ARM_CONTEXT_H_

#include "msd_arm_connection.h"

class MsdArmContext : public msd_context_t {
 public:
  MsdArmContext(std::weak_ptr<MsdArmConnection> connection) : connection_(connection) {
    connection.lock()->IncrementContextCount();
  }
  ~MsdArmContext() {
    auto locked = connection_.lock();
    if (locked) {
      locked->DecrementContextCount();
    }
  }

  std::weak_ptr<MsdArmConnection> connection() { return connection_; }

 private:
  std::weak_ptr<MsdArmConnection> connection_;
};

#endif  // GARNET_DRIVERS_GPU_MSD_ARM_MALI_SRC_MSD_ARM_CONTEXT_H_
