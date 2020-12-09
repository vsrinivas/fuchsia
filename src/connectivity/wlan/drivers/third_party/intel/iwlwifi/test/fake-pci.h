// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_PCI_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_PCI_H_

#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/bti.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/internal.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mac-device.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/pcie_device.h"

namespace wlan::testing {

constexpr int test_device_id = 0x095a;
constexpr int test_subsys_device_id = 0x9e10;

// Fake PCI to emulate calls during bind.
class FakePci {
 public:
  FakePci() {
    proto_ops_.config_read16 = PciConfigRead16;
    proto_ops_.get_device_info = PciGetDeviceInfo;
    proto_ops_.enable_bus_master = PciEnableBusMaster;
    proto_ops_.get_bti = PciGetBti;
    proto_ops_.get_bar = PciGetBar;
    proto_ops_.config_write8 = PciConfigWrite8;
    proto_ops_.config_read8 = PciConfigRead8;
    proto_ops_.configure_irq_mode = PciConfigureIrqMode;
    proto_ops_.map_interrupt = PciMapInterrupt;

    proto_.ctx = this;
    proto_.ops = &proto_ops_;
  }

  const pci_protocol_t* GetProto() const { return &proto_; }

  static zx_status_t PciConfigureIrqMode(void* ctx, uint32_t requested_irq_count) { return ZX_OK; }
  static zx_status_t PciConfigRead8(void* ctx, uint16_t offset, uint8_t* out_value) {
    return ZX_OK;
  }
  static zx_status_t PciConfigWrite8(void* ctx, uint16_t offset, uint8_t value) { return ZX_OK; }
  static zx_status_t PciEnableBusMaster(void* ctx, bool enable) { return ZX_OK; }

  static zx_status_t PciGetDeviceInfo(void* ctx, zx_pcie_device_info_t* out_info) {
    out_info->device_id = test_device_id;
    return ZX_OK;
  }
  static zx_status_t PciConfigRead16(void* ctx, uint16_t offset, uint16_t* out_value) {
    *out_value = test_subsys_device_id;
    return ZX_OK;
  }

  static zx_status_t PciMapInterrupt(void* ctx, uint32_t which_irq, zx_handle_t* out_handle) {
    return ZX_OK;
  }

  static zx_status_t PciGetBti(void* ctx, uint32_t index, zx_handle_t* out_bti) {
    zx::bti* bti = (zx::bti*)out_bti;
    zx_status_t status = fake_bti_create(bti->reset_and_get_address());
    if (status != ZX_OK) {
      return status;
    }

    return ZX_OK;
  }

  static zx_status_t PciGetBar(void* ctx, uint32_t bar_id, zx_pci_bar_t* out_res) {
    zx::vmo vmo_bar;
    const int vmo_size = 4096u;
    zx_status_t status = zx::vmo::create(vmo_size, 0u, &vmo_bar);
    if (status != ZX_OK) {
      return status;
    }

    out_res->id = bar_id;
    out_res->type = ZX_PCI_BAR_TYPE_MMIO;
    out_res->size = vmo_size;
    out_res->handle = vmo_bar.release();

    return ZX_OK;
  }

 private:
  pci_protocol_t proto_;
  pci_protocol_ops_t proto_ops_;
};

class FakePcieDdkTester : public fake_ddk::Bind {
 public:
  FakePcieDdkTester() : fake_ddk::Bind() {
    // PCI is the only protocol of interest here, so we create and pass an
    // fbl:Array of size 1.
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
    protocols[0] = {ZX_PROTOCOL_PCI,
                    *reinterpret_cast<const fake_ddk::Protocol*>(fake_pci_.GetProto())};
    SetProtocols(std::move(protocols));
  }

  wlan::iwlwifi::PcieDevice* dev() { return dev_; }
  const std::vector<wlan::iwlwifi::MacDevice*>& macdevs() { return macdevs_; }
  fake_ddk::Bind& ddk() { return *this; }
  FakePci fake_pci_;

 protected:
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    zx_status_t ret = Bind::DeviceAdd(drv, parent, args, out);
    if (ret == ZX_OK) {
      // On successful DeviceAdd() we save off the devices so that we can access them in the test
      // to take subsequent actions or to release its resources at the end of the test.
      if (parent == fake_ddk::kFakeParent) {
        // The top node for iwlwifi will be the PcieDevice
        dev_ = static_cast<wlan::iwlwifi::PcieDevice*>(args->ctx);
      } else {
        // Everything else (i.e. children) will be of type MacDevice, created via
        // the WlanphyImplCreateIface() call.
        macdevs_.push_back(static_cast<wlan::iwlwifi::MacDevice*>(args->ctx));
      }
    }
    return ret;
  }

 private:
  wlan::iwlwifi::PcieDevice* dev_;
  std::vector<wlan::iwlwifi::MacDevice*> macdevs_;
};
}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_PCI_H_
