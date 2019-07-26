// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "allocation.h"
#include "common.h"
#include "config.h"
#include "device.h"
#include "ref_counted.h"
#include "upstream_node.h"
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <region-alloc/region-alloc.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>

namespace pci {

class Bridge : public pci::Device, public UpstreamNode {
 public:
  static zx_status_t Create(zx_device_t* parent, std::unique_ptr<Config>&& config,
                            UpstreamNode* upstream, BusLinkInterface* bli, uint8_t mbus_id,
                            fbl::RefPtr<pci::Bridge>* out_bridge);
  // Derived device objects need to have refcounting implemented
  PCI_IMPLEMENT_REFCOUNTED;

  // Disallow copying, assigning and moving.
  Bridge(const Bridge&) = delete;
  Bridge(Bridge&&) = delete;
  Bridge& operator=(const Bridge&) = delete;
  Bridge& operator=(Bridge&&) = delete;

  // UpstreamNode overrides
  PciAllocator& mmio_regions() final { return mmio_regions_; }
  PciAllocator& pf_mmio_regions() final { return pf_mmio_regions_; }
  PciAllocator& pio_regions() final { return pio_regions_; }

  // Property accessors
  uint64_t pf_mem_base() const { return pf_mem_base_; }
  uint64_t pf_mem_limit() const { return pf_mem_limit_; }
  uint32_t mem_base() const { return mem_base_; }
  uint32_t mem_limit() const { return mem_limit_; }
  uint32_t io_base() const { return io_base_; }
  uint32_t io_limit() const { return io_limit_; }
  bool supports_32bit_pio() const { return supports_32bit_pio_; }

  // Device overrides
  void Dump() const final;
  void Unplug() final TA_EXCL(dev_lock_);

 protected:
  zx_status_t ConfigureBars() final TA_EXCL(dev_lock_);
  zx_status_t AllocateBridgeWindowsLocked() TA_REQ(dev_lock_);
  void Disable() final TA_EXCL(dev_lock_);

 private:
  Bridge(zx_device_t* parent, std::unique_ptr<Config>&&, UpstreamNode* upstream,
         BusLinkInterface* bli, uint8_t managed_bus_id);
  zx_status_t Init() TA_EXCL(dev_lock_);

  zx_status_t ParseBusWindowsLocked() TA_REQ(dev_lock_);

  PciRegionAllocator mmio_regions_;
  PciRegionAllocator pf_mmio_regions_;
  PciRegionAllocator pio_regions_;

  uint64_t pf_mem_base_;
  uint64_t pf_mem_limit_;
  uint32_t mem_base_;
  uint32_t mem_limit_;
  uint32_t io_base_;
  uint32_t io_limit_;
  bool supports_32bit_pio_;
};

}  // namespace pci
