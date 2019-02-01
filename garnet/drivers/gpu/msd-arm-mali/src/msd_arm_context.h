// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_ARM_CONTEXT_H_
#define MSD_ARM_CONTEXT_H_

#include "msd_arm_connection.h"

class MsdArmContext : public msd_context_t {
public:
    MsdArmContext(std::weak_ptr<MsdArmConnection> connection) : connection_(connection) {}

    std::weak_ptr<MsdArmConnection> connection() { return connection_; }

private:
    std::weak_ptr<MsdArmConnection> connection_;
};

#endif // MSD_ARM_CONTEXT_H_
