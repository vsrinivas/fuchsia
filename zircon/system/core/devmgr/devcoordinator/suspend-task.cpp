// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "suspend-task.h"

#include "coordinator.h"

namespace devmgr {

SuspendTask::SuspendTask(fbl::RefPtr<Device> device, uint32_t flags, Completion completion)
    : Task(device->coordinator->dispatcher(), std::move(completion)), device_(std::move(device)),
      flags_(flags) {}

SuspendTask::~SuspendTask() = default;

fbl::RefPtr<SuspendTask> SuspendTask::Create(fbl::RefPtr<Device> device, uint32_t flags,
                                             Completion completion) {
    return fbl::MakeRefCounted<SuspendTask>(std::move(device), flags, std::move(completion));
}

void SuspendTask::Run() {
    bool found_more_dependencies = false;
    for (auto& child : device_->children()) {
        // Use a switch statement here so that this gets reconsidered if we add
        // more states.
        switch (child.state()) {
        case Device::State::kSuspended: continue;
        case Device::State::kActive: break;
        }
        auto task = SuspendTask::Create(fbl::WrapRefPtr(&child), flags_);
        AddDependency(std::move(task));
        found_more_dependencies = true;
    }
    if (found_more_dependencies) {
        return;
    }

    // Handle the device proxy, if it exists, after children since they might
    // depend on it.
    if (device_->proxy != nullptr) {
        switch (device_->proxy->state()) {
        case Device::State::kSuspended: break;
        case Device::State::kActive: {
            auto task = SuspendTask::Create(device_->proxy, flags_);
            AddDependency(std::move(task));
            return;
        }
        }
    }

    // Check if this device is not in a devhost.  This happens for the
    // top-level devices like /sys provided by devcoordinator
    if (device_->host() == nullptr) {
        return Complete(ZX_OK);
    }

    auto completion = [this](zx_status_t status) {
        Complete(status);
    };
    zx_status_t status = device_->SendSuspend(flags_, std::move(completion));
    if (status != ZX_OK) {
        Complete(status);
    }
}

} // namespace devmgr
