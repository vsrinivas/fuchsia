// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-resources.h"

namespace {

template <typename T>
bool CopyResources(size_t in_count, const T* in_list, fbl::Array<T>* out) {
    if (!in_count) return true;
    fbl::AllocChecker ac;
    out->reset(new (&ac) T[in_count], in_count);
    if (!ac.check()) return false;
    memcpy(out->begin(), in_list, in_count * sizeof(T));
    return true;
}

} // namespace

namespace platform_bus {

zx_status_t DeviceResources::Init(const pbus_dev_t* pdev, uint32_t* next_index) {
    if (!CopyResources(pdev->mmio_count, pdev->mmios, &mmios_) ||
        !CopyResources(pdev->irq_count, pdev->irqs, &irqs_) ||
        !CopyResources(pdev->gpio_count, pdev->gpios, &gpios_) ||
        !CopyResources(pdev->i2c_channel_count, pdev->i2c_channels, &i2c_channels_) ||
        !CopyResources(pdev->clk_count, pdev->clks, &clks_) ||
        !CopyResources(pdev->bti_count, pdev->btis, &btis_) ||
        !CopyResources(pdev->metadata_count, pdev->metadata, &metadata_)) {
        return ZX_ERR_NO_MEMORY;
    }

    if (pdev->child_count) {
        fbl::AllocChecker ac;
        children_.reserve(pdev->child_count, &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        for (uint32_t i = 0; i < pdev->child_count; i++) {
            DeviceResources dr((*next_index)++);
            auto status = dr.Init(&pdev->children[i], next_index);
            if (status != ZX_OK) {
                return status;
            }
            children_.push_back(fbl::move(dr));
        }
    }

    return ZX_OK;
}

zx_status_t DeviceResources::Init(const pbus_dev_t* pdev) {
    uint32_t next_index = index_ + 1;
    return Init(pdev, &next_index);
}

size_t DeviceResources::DeviceCount() const {
    size_t result = 1;
    for (auto& dr : children_) {
        result += dr.DeviceCount();
    }
    return result;
}

void DeviceResources::BuildDeviceIndex(fbl::Vector<const DeviceResources*>* index) const {
    index->push_back(this);
    for (DeviceResources& dr : children_) {
        dr.BuildDeviceIndex(index);
    }
}

} // namespace platform_bus
