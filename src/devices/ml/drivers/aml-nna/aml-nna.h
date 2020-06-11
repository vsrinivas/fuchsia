// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_ML_DRIVERS_AML_NNA_AML_NNA_H_
#define SRC_DEVICES_ML_DRIVERS_AML_NNA_AML_NNA_H_

#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/mmio/mmio.h>
#include <zircon/fidl.h>

#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <hw/reg.h>

namespace aml_nna {

class AmlNnaDevice;
using AmlNnaDeviceType = ddk::Device<AmlNnaDevice, ddk::GetProtocolable, ddk::UnbindableNew>;

class AmlNnaDevice : public AmlNnaDeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_NNA> {
 public:
  explicit AmlNnaDevice(zx_device_t* parent, ddk::MmioBuffer hiu_mmio, ddk::MmioBuffer power_mmio,
                        ddk::MmioBuffer memory_pd_mmio, pdev_protocol_t proto)
      : AmlNnaDeviceType(parent),
        pdev_(parent),
        hiu_mmio_(std::move(hiu_mmio)),
        power_mmio_(std::move(power_mmio)),
        memory_pd_mmio_(std::move(memory_pd_mmio)) {
    memcpy(&parent_pdev_, &proto, sizeof(proto));
  }
  static zx_status_t Create(void* ctx, zx_device_t* parent);
  void Init();

  // Methods required by the ddk.
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  void DdkRelease();
  void DdkUnbindNew(ddk::UnbindTxn txn);

 private:
  ddk::PDev pdev_;
  ddk::MmioBuffer hiu_mmio_;
  ddk::MmioBuffer power_mmio_;
  ddk::MmioBuffer memory_pd_mmio_;

  pdev_protocol_t parent_pdev_;
};

}  // namespace aml_nna

#endif  // SRC_DEVICES_ML_DRIVERS_AML_NNA_AML_NNA_H_
