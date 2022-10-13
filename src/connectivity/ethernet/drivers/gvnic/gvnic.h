// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_GVNIC_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_GVNIC_H_

#include <lib/device-protocol/pci.h>
#include <lib/dma-buffer/buffer.h>
#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>

#include "src/connectivity/ethernet/drivers/gvnic/abi.h"

#define GVNIC_VERSION "v1.awesome"

namespace gvnic {

class Gvnic;
using DeviceType = ddk::Device<Gvnic, ddk::Initializable>;
class Gvnic : public DeviceType {
 public:
  explicit Gvnic(zx_device_t* parent) : DeviceType(parent) {}
  virtual ~Gvnic() = default;

  static __WARN_UNUSED_RESULT zx_status_t Bind(void* ctx, zx_device_t* dev);
  __WARN_UNUSED_RESULT zx_status_t Bind();

  // ::ddk::Device implementation.
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

  // For inspect test.
  zx::vmo inspect_vmo() { return inspect_.DuplicateVmo(); }

 private:
  __WARN_UNUSED_RESULT zx_status_t SetUpPci();
  __WARN_UNUSED_RESULT zx_status_t MapBars();

  ddk::Pci pci_;
  std::unique_ptr<dma_buffer::BufferFactory> buffer_factory_;
  std::optional<fdf::MmioBuffer> reg_mmio_;
  std::optional<fdf::MmioBuffer> doorbell_mmio_;

  GvnicRegisters regs_;
  zx::bti bti_;

  inspect::Inspector inspect_;
  // `is_bound` is an example property. Replace this with useful properties of the device.
  inspect::BoolProperty is_bound = inspect_.GetRoot().CreateBool("is_bound", false);
};

}  // namespace gvnic

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_GVNIC_H_
