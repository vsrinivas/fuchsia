// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_H_
#define POWER_MANAGER_H_

#include <magma_util/register_io.h>

class PowerManager {
public:
    PowerManager(RegisterIo* io);

    void EnableCores(RegisterIo* io, uint64_t shader_bitmask);

    void ReceivedPowerInterrupt(RegisterIo* io);

    uint64_t shader_ready_status() const { return shader_ready_status_; }
    uint64_t l2_ready_status() const { return l2_ready_status_; }

private:
    friend class TestMsdArmDevice;

    uint64_t tiler_ready_status_ = 0;
    uint64_t l2_ready_status_ = 0;
    uint64_t shader_ready_status_ = 0;
};

#endif // POWER_MANAGER_H_
