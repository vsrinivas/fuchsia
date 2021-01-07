// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370-usb-phy.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <hw/reg.h>
#include <soc/as370/as370-reset.h>
#include <soc/as370/as370-usb.h>
#include <soc/vs680/vs680-reset.h>
#include <soc/vs680/vs680-usb.h>

#include "src/devices/usb/drivers/as370-usb-phy/as370_usb_phy_bind.h"

namespace as370_usb_phy {

void UsbPhy::ResetPhy() {
  auto* mmio = &*reset_mmio_;
  auto reset = as370::GblPerifStickyResetN::Get().ReadFrom(mmio);
  reset.set_usbOtgPhyreset(0).WriteTo(mmio);
  reset.set_usbOtgPrstn(1).WriteTo(mmio);
  usleep(10);
  reset.set_usbOtgHresetn(1).WriteTo(mmio);
  usleep(100);
}

zx_status_t UsbPhy::InitPhy() {
  auto* mmio = &*usbphy_mmio_;

  if (did_ == PDEV_DID_VS680_USB_PHY) {
    auto* resetmmio = &*reset_mmio_;

    vs680::ClockReg700::Get().ReadFrom(resetmmio).set_usb0coreclkEn(1).WriteTo(resetmmio);

    // 1.  Trigger usb0SyncReset (set usb0SyncReset to 1). No read back because no read modify write
    //     that could trigger other agent reset
    vs680::Gbl_perifReset::Get().FromValue(0).set_usb0SyncReset(1).WriteTo(resetmmio);

    // 2.  Assert sticky resets to USBOTG PHY and MAC. (set usb0PhyRstn, usb0CoreRstn
    //     and usb0MahbRstn to 0)
    vs680::Gbl_perifStickyResetN::Get()
        .ReadFrom(resetmmio)
        .set_usb0PhyRstn(0)
        .set_usb0CoreRstn(0)
        .set_usb0MahbRstn(0)
        .WriteTo(resetmmio);

    // 3.1.  Program USB_CTRL0
    vs680::USB_PHY_CTRL0::Get().FromValue(0).set_value(0x533DADF0).WriteTo(mmio);
    // 3.2.  Program USB_CTRL1
    vs680::USB_PHY_CTRL1::Get().FromValue(0).set_value(0x01B10000).WriteTo(mmio);

    // 4.  De-assert sticky resets PHY only. (set usb0PhyRstn to 1)
    vs680::Gbl_perifStickyResetN::Get().ReadFrom(resetmmio).set_usb0PhyRstn(1).WriteTo(resetmmio);

    // 5.  Wait more than 45us
    usleep(45);

    // 6.  De-assert core(set usb0CoreRstn and usb0MahbRstn to 1).
    vs680::Gbl_perifStickyResetN::Get()
        .ReadFrom(resetmmio)
        .set_usb0CoreRstn(1)
        .set_usb0MahbRstn(1)
        .WriteTo(resetmmio);
    usleep(100);

    return ZX_OK;
  } else {
    as370::USB_PHY_CTRL0::Get().FromValue(0).set_value(0x0EB35E84).WriteTo(mmio);
    as370::USB_PHY_CTRL1::Get().FromValue(0).set_value(0x80E9F004).WriteTo(mmio);

    ResetPhy();

    uint32_t count = 10000;
    while (count) {
      if (as370::USB_PHY_RB::Get().ReadFrom(mmio).clk_rdy()) {
        break;
      }
      usleep(1);
      count--;
    }

    return count ? 0 : ZX_ERR_TIMED_OUT;

    return ZX_OK;
  }
}

zx_status_t UsbPhy::Create(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<UsbPhy>(parent);
  auto status = dev->Init();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();
  return ZX_OK;
}

zx_status_t UsbPhy::AddDwc2Device() {
  if (dwc2_device_) {
    zxlogf(ERROR, "UsbPhy::AddDwc2Device: device already exists!");
    return ZX_ERR_BAD_STATE;
  }

  fbl::AllocChecker ac;
  dwc2_device_ = fbl::make_unique_checked<Dwc2Device>(&ac, zxdev());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_USB_DWC2},
  };

  return dwc2_device_->DdkAdd(
      ddk::DeviceAddArgs("dwc2").set_props(props).set_proto_id(ZX_PROTOCOL_USB_PHY));
}

zx_status_t UsbPhy::RemoveDwc2Device() {
  if (dwc2_device_ == nullptr) {
    zxlogf(ERROR, "UsbPhy::RemoveDwc2Device: device does not exist!");
    return ZX_ERR_BAD_STATE;
  }

  // devmgr will own the device until it is destroyed.
  __UNUSED auto* dev = dwc2_device_.release();
  dev->DdkAsyncRemove();

  return ZX_OK;
}

zx_status_t UsbPhy::Init() {
  if (!pdev_.is_valid()) {
    zxlogf(ERROR, "UsbPhy::Init: could not get platform device protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto status = pdev_.MapMmio(0, &usbphy_mmio_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "UsbPhy::Init: MapMmio failed for usbphy_mmio_");
    return status;
  }
  status = pdev_.MapMmio(1, &reset_mmio_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "UsbPhy::Init: MapMmio failed for reset_mmio_");
    return status;
  }

  pdev_device_info_t info;
  status = pdev_.GetDeviceInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "UsbPhy::Init: GetDeviceInfo failed");
    return status;
  }
  did_ = info.did;

  status = InitPhy();
  if (status != ZX_OK) {
    zxlogf(ERROR, "UsbPhy::Init: InitPhy() failed");
    return status;
  }

  const char* name = "as370-usb-phy";
  if (did_ == PDEV_DID_VS680_USB_PHY) {
    name = "vs680-usb-phy";
  }
  status = DdkAdd(name, DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "UsbPhy::Init: DdkAdd() failed");
    return status;
  }

  AddDwc2Device();

  return ZX_OK;
}

void UsbPhy::DdkUnbind(ddk::UnbindTxn txn) {
  RemoveDwc2Device();
  txn.Reply();
}

void UsbPhy::DdkRelease() { delete this; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = UsbPhy::Create;
  return ops;
}();

}  // namespace as370_usb_phy

ZIRCON_DRIVER(as370_usb_phy, as370_usb_phy::driver_ops, "zircon", "0.1");
