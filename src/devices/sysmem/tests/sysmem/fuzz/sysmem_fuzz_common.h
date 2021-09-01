// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_TESTS_SYSMEM_FUZZ_SYSMEM_FUZZ_COMMON_H_
#define SRC_DEVICES_SYSMEM_TESTS_SYSMEM_FUZZ_SYSMEM_FUZZ_COMMON_H_

#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <lib/fake_ddk/fake_ddk.h>

#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"
#include "src/devices/sysmem/drivers/sysmem/device.h"
#include "src/devices/sysmem/drivers/sysmem/driver.h"

class FakePBus : public ddk::PBusProtocol<FakePBus, ddk::base_protocol> {
 public:
  FakePBus() : proto_({&pbus_protocol_ops_, this}) {}
  const pbus_protocol_t* proto() const { return &proto_; }
  zx_status_t PBusDeviceAdd(const pbus_dev_t* dev) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusRegisterProtocol(uint32_t proto_id, const uint8_t* protocol,
                                   size_t protocol_size) {
    return ZX_OK;
  }
  zx_status_t PBusGetBoardInfo(pdev_board_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusSetBoardInfo(const pbus_board_info_t* info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusSetBootloaderInfo(const pbus_bootloader_info_t* info) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusCompositeDeviceAdd(const pbus_dev_t* dev,
                                     /* const device_fragment_t* */ uint64_t fragments_list,
                                     size_t fragments_count, const char* primary_fragment) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusAddComposite(const pbus_dev_t* dev,
                               /* const device_fragment_t* */ uint64_t fragments_list,
                               size_t fragments_count, const char* primary_fragment) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusRegisterSysSuspendCallback(const pbus_sys_suspend_t* suspend_cbin) {
    return ZX_ERR_NOT_SUPPORTED;
  }

 private:
  pbus_protocol_t proto_;
};

class FakeDdkSysmem {
 public:
  ~FakeDdkSysmem();
  fake_ddk::Bind& ddk() { return ddk_; }

  bool Init();
  zx::status<fidl::ClientEnd<fuchsia_sysmem::Allocator>> Connect();

 protected:
  bool initialized_ = false;
  sysmem_driver::Driver sysmem_ctx_;
  sysmem_driver::Device sysmem_{fake_ddk::kFakeParent, &sysmem_ctx_};

  FakePBus pbus_;
  fake_pdev::FakePDev pdev_;
  // ddk must be destroyed before sysmem because it may be executing messages against sysmem on
  // another thread.
  fake_ddk::Bind ddk_;
};

#endif  // SRC_DEVICES_SYSMEM_TESTS_SYSMEM_FUZZ_SYSMEM_FUZZ_COMMON_H_
