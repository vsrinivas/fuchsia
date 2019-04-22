// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "device.h"
#include "task.h"

namespace devmgr {

class SuspendTask final : public Task {
public:
    static fbl::RefPtr<SuspendTask> Create(fbl::RefPtr<Device> device, uint32_t flags,
                                           Completion completion = nullptr);

    // Don/t invoke this, use Create
    SuspendTask(fbl::RefPtr<Device> device, uint32_t flags, Completion completion);

    ~SuspendTask() final;
private:
    void Run() final;

    // The device being suspended
    fbl::RefPtr<Device> device_;
    // The target suspend flags
    uint32_t flags_;
};

} // namespace devmgr
