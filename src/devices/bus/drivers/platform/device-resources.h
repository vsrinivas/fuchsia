// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_DRIVERS_PLATFORM_DEVICE_RESOURCES_H_
#define SRC_DEVICES_BUS_DRIVERS_PLATFORM_DEVICE_RESOURCES_H_

#include <fuchsia/hardware/platform/bus/c/banjo.h>

#include <fbl/array.h>
#include <fbl/vector.h>

#include "proxy-protocol.h"

namespace platform_bus {

class DeviceResources {
 public:
  DeviceResources() {}

  zx_status_t Init(const pbus_dev_t* pdev);

  // Platform bus resources copied from the pbus_dev_t struct from the board driver.
  inline const pbus_mmio_t& mmio(size_t i) const { return mmios_[i]; }
  inline const pbus_irq_t& irq(size_t i) const { return irqs_[i]; }
  inline const pbus_bti_t& bti(size_t i) const { return btis_[i]; }
  inline const pbus_smc_t& smc(size_t i) const { return smcs_[i]; }
  inline const pbus_metadata_t& metadata(size_t i) const { return metadata_[i]; }
  inline const pbus_boot_metadata_t& boot_metadata(size_t i) const { return boot_metadata_[i]; }

  // Counts for the above resource lists.
  inline size_t mmio_count() const { return mmios_.size(); }
  inline size_t irq_count() const { return irqs_.size(); }
  inline size_t bti_count() const { return btis_.size(); }
  inline size_t smc_count() const { return smcs_.size(); }
  inline size_t metadata_count() const { return metadata_.size(); }
  inline size_t boot_metadata_count() const { return boot_metadata_.size(); }

 private:
  bool CopyMetadataDataBuffers();

  // Platform bus resources copied from the pbus_dev_t struct from the board driver.
  fbl::Array<pbus_mmio_t> mmios_;
  fbl::Array<pbus_irq_t> irqs_;
  fbl::Array<pbus_bti_t> btis_;
  fbl::Array<pbus_smc_t> smcs_;
  fbl::Array<pbus_metadata_t> metadata_;
  fbl::Array<pbus_boot_metadata_t> boot_metadata_;

  // Backing buffers for each |metadata_| data_buffer pointer.
  fbl::Array<fbl::Array<uint8_t>> metadata_data_buffers_;
};

}  // namespace platform_bus

#endif  // SRC_DEVICES_BUS_DRIVERS_PLATFORM_DEVICE_RESOURCES_H_
