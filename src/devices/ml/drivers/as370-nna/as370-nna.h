// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_ML_DRIVERS_AS370_NNA_AS370_NNA_H_
#define SRC_DEVICES_ML_DRIVERS_AS370_NNA_AS370_NNA_H_

#include <fidl/fuchsia.hardware.registers/cpp/wire.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <zircon/fidl.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace as370_nna {

class As370NnaDevice;
using As370NnaDeviceType = ddk::Device<As370NnaDevice, ddk::GetProtocolable>;

class As370NnaDevice : public As370NnaDeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_NNA> {
 public:
  explicit As370NnaDevice(zx_device_t* parent,
                          fidl::WireSyncClient<fuchsia_hardware_registers::Device> global_registers,
                          ddk::PDev pdev)
      : As370NnaDeviceType(parent),
        pdev_(std::move(pdev)),
        global_registers_(std::move(global_registers)) {
    pdev_.GetProto(&parent_pdev_);
  }
  static zx_status_t Create(void* ctx, zx_device_t* parent);
  zx_status_t Init();

  // Methods required by the ddk.
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  void DdkRelease();

 private:
  ddk::PDev pdev_;
  fidl::WireSyncClient<fuchsia_hardware_registers::Device> global_registers_;

  pdev_protocol_t parent_pdev_;
};

}  // namespace as370_nna

#endif  // SRC_DEVICES_ML_DRIVERS_AS370_NNA_AS370_NNA_H_
