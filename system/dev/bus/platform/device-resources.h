// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform-bus.h>
#include <fbl/array.h>
#include <fbl/vector.h>

#include "proxy-protocol.h"

namespace platform_bus {

class DeviceResources {
public:
    DeviceResources(uint32_t index)
        : index_(index) {}

    // Initializes this instance from the resources in the provided pbus_dev_t.
    // next_index keeps track of the index to be assigned to the children while inflating the tree.
    zx_status_t Init(const pbus_dev_t* pdev, uint32_t* next_index);
    // Variant of Init() used for initializing the root of the tree.
    zx_status_t Init(const pbus_dev_t* pdev);

    // Returns the total number of devices, including this device and all children.
    size_t DeviceCount() const;
    // Builds a flattened list of all DeviceResources.
    void BuildDeviceIndex(fbl::Vector<const DeviceResources*>* index) const;

    // Returns device ID for this device.
    inline uint32_t index() const { return index_; }

    // Returns the index of the ith child of this device.
    inline uint32_t child_index(uint32_t i) const { return children_[i].index_; }

    // Platform bus resources copied from the pbus_dev_t struct from the board driver.
    inline const pbus_mmio_t& mmio(size_t i) const { return mmios_[i]; }
    inline const pbus_irq_t& irq(size_t i) const { return irqs_[i]; }
    inline const pbus_gpio_t& gpio(size_t i) const { return gpios_[i]; }
    inline const pbus_i2c_channel_t& i2c_channel(size_t i) const { return i2c_channels_[i]; }
    inline const pbus_clk_t& clk(size_t i) const { return clks_[i]; }
    inline const pbus_bti_t& bti(size_t i) const { return btis_[i]; }
    inline const pbus_smc_t& smc(size_t i) const { return smcs_[i]; }
    inline const pbus_metadata_t& metadata(size_t i) const { return metadata_[i]; }
    inline const pbus_boot_metadata_t& boot_metadata(size_t i) const { return boot_metadata_[i]; }
    inline const uint32_t* protocols() const { return protocols_.begin(); }

    // Counts for the above resource lists.
    inline size_t mmio_count() const { return mmios_.size(); }
    inline size_t irq_count() const { return irqs_.size(); }
    inline size_t gpio_count() const { return gpios_.size(); }
    inline size_t i2c_channel_count() const { return i2c_channels_.size(); }
    inline size_t clk_count() const { return clks_.size(); }
    inline size_t bti_count() const { return btis_.size(); }
    inline size_t smc_count() const { return smcs_.size(); }
    inline size_t metadata_count() const { return metadata_.size(); }
    inline size_t boot_metadata_count() const { return boot_metadata_.size(); }
    inline size_t child_count() const { return children_.size(); }
    inline size_t protocol_count() const { return protocols_.size(); }

private:
    // Index of this DeviceResources instance in PlatformDevice::device_index_.
    const uint32_t index_;

    // Platform bus resources copied from the pbus_dev_t struct from the board driver.
    fbl::Array<pbus_mmio_t> mmios_;
    fbl::Array<pbus_irq_t> irqs_;
    fbl::Array<pbus_gpio_t> gpios_;
    fbl::Array<pbus_i2c_channel_t> i2c_channels_;
    fbl::Array<pbus_clk_t> clks_;
    fbl::Array<pbus_bti_t> btis_;
    fbl::Array<pbus_smc_t> smcs_;
    fbl::Array<pbus_metadata_t> metadata_;
    fbl::Array<pbus_boot_metadata_t> boot_metadata_;
    fbl::Array<uint32_t> protocols_;

    // Resources for children of this device.
    fbl::Vector<DeviceResources> children_;
};

} // namespace platform_bus
