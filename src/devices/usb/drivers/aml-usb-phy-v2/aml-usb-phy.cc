// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-usb-phy.h"

#include <assert.h>
#include <lib/zx/time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/errors.h>

#include <cstdio>
#include <sstream>
#include <string>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <hw/reg.h>
#include <soc/aml-common/aml-g12-reset.h>

#include "usb-phy-regs.h"

namespace aml_usb_phy {

// Based on set_usb_pll() in phy-aml-new-usb2-v2.c
void AmlUsbPhy::InitPll(ddk::MmioBuffer* mmio) {
  PLL_REGISTER_40::Get()
      .FromValue(0)
      .set_value(pll_settings_[0])
      .set_enable(1)
      .set_reset(1)
      .WriteTo(mmio);

  PLL_REGISTER::Get(0x44).FromValue(pll_settings_[1]).WriteTo(mmio);

  PLL_REGISTER::Get(0x48).FromValue(pll_settings_[2]).WriteTo(mmio);

  zx::nanosleep(zx::deadline_after(zx::usec(100)));

  PLL_REGISTER_40::Get()
      .FromValue(0)
      .set_value(pll_settings_[0])
      .set_enable(1)
      .set_reset(0)
      .WriteTo(mmio);

  // PLL

  zx::nanosleep(zx::deadline_after(zx::usec(100)));

  PLL_REGISTER::Get(0x50).FromValue(pll_settings_[3]).WriteTo(mmio);

  PLL_REGISTER::Get(0x10).FromValue(pll_settings_[4]).WriteTo(mmio);

  // Recovery state
  PLL_REGISTER::Get(0x38).FromValue(0).WriteTo(mmio);

  PLL_REGISTER::Get(0x34).FromValue(pll_settings_[5]).WriteTo(mmio);

  // Disconnect threshold
  PLL_REGISTER::Get(0xc).FromValue(0x3c).WriteTo(mmio);

  // Tuning

  zx::nanosleep(zx::deadline_after(zx::usec(100)));

  PLL_REGISTER::Get(0x38).FromValue(pll_settings_[6]).WriteTo(mmio);

  PLL_REGISTER::Get(0x34).FromValue(pll_settings_[5]).WriteTo(mmio);

  zx::nanosleep(zx::deadline_after(zx::usec(100)));
}

zx_status_t AmlUsbPhy::InitPhy() {
  auto* reset_mmio = &*reset_mmio_;
  auto* usbctrl_mmio = &*usbctrl_mmio_;

  // first reset USB
  auto reset_1_level = aml_reset::RESET_1::GetLevel().ReadFrom(reset_mmio);
  // The bits being manipulated here are not documented.
  reset_1_level.set_reg_value(reset_1_level.reg_value() | (0x3 << 16));
  reset_1_level.WriteTo(reset_mmio);

  // amlogic_new_usbphy_reset_v2()
  auto reset_1 = aml_reset::RESET_1::Get().ReadFrom(reset_mmio);
  reset_1.set_usb(1);
  reset_1.WriteTo(reset_mmio);
  // FIXME(voydanoff) this delay is very long, but it is what the Amlogic Linux kernel is doing.
  zx::nanosleep(zx::deadline_after(zx::usec(500)));

  // amlogic_new_usb2_init()
  for (int i = 0; i < 2; i++) {
    auto u2p_r0 = U2P_R0_V2::Get(i).ReadFrom(usbctrl_mmio);
    u2p_r0.set_por(1);
    u2p_r0.set_host_device(1);
    if (i == 1) {
      u2p_r0.set_idpullup0(1);
      u2p_r0.set_drvvbus0(1);
    }
    u2p_r0.WriteTo(usbctrl_mmio);

    zx::nanosleep(zx::deadline_after(zx::usec(10)));

    // amlogic_new_usbphy_reset_phycfg_v2()
    reset_1.ReadFrom(reset_mmio);
    // The bit being manipulated here is not documented.
    reset_1.set_reg_value(reset_1.reg_value() | (1 << 16));
    reset_1.WriteTo(reset_mmio);

    zx::nanosleep(zx::deadline_after(zx::usec(50)));

    auto u2p_r1 = U2P_R1_V2::Get(i);

    int count = 0;
    while (!u2p_r1.ReadFrom(usbctrl_mmio).phy_rdy()) {
      // wait phy ready max 1ms, common is 100us
      if (count > 200) {
        zxlogf(ERROR, "AmlUsbPhy::InitPhy U2P_R1_PHY_RDY wait failed");
        break;
      }

      count++;
      zx::nanosleep(zx::deadline_after(zx::usec(5)));
    }
  }

  return ZX_OK;
}

zx_status_t AmlUsbPhy::InitOtg() {
  auto* mmio = &*usbctrl_mmio_;

  USB_R1_V2::Get().ReadFrom(mmio).set_u3h_fladj_30mhz_reg(0x20).WriteTo(mmio);

  USB_R5_V2::Get().ReadFrom(mmio).set_iddig_en0(1).set_iddig_en1(1).set_iddig_th(255).WriteTo(mmio);

  return ZX_OK;
}

void AmlUsbPhy::SetMode(UsbMode mode, SetModeCompletion completion) {
  ZX_DEBUG_ASSERT(mode == UsbMode::HOST || mode == UsbMode::PERIPHERAL);
  // Only the irq thread calls |SetMode|, and it should have waited for the
  // previous call to |SetMode| to complete.
  ZX_DEBUG_ASSERT(!set_mode_completion_);
  auto ac = fbl::MakeAutoCall([&]() {
    if (completion)
      completion();
  });

  if (mode == mode_)
    return;

  auto* usbctrl_mmio = &*usbctrl_mmio_;

  auto r0 = USB_R0_V2::Get().ReadFrom(usbctrl_mmio);
  if (mode == UsbMode::HOST) {
    r0.set_u2d_act(0);
  } else {
    r0.set_u2d_act(1);
    r0.set_u2d_ss_scaledown_mode(0);
  }
  r0.WriteTo(usbctrl_mmio);

  USB_R4_V2::Get()
      .ReadFrom(usbctrl_mmio)
      .set_p21_sleepm0(mode == UsbMode::PERIPHERAL)
      .WriteTo(usbctrl_mmio);

  U2P_R0_V2::Get(0)
      .ReadFrom(usbctrl_mmio)
      .set_host_device(mode == UsbMode::HOST)
      .set_por(0)
      .WriteTo(usbctrl_mmio);

  zx::nanosleep(zx::deadline_after(zx::usec(500)));

  auto old_mode = mode_;
  mode_ = mode;

  if (old_mode == UsbMode::UNKNOWN) {
    // One time PLL initialization
    InitPll(&*usbphy20_mmio_);
    InitPll(&*usbphy21_mmio_);
  } else {
    auto* phy_mmio = &*usbphy21_mmio_;

    PLL_REGISTER::Get(0x38)
        .FromValue(mode == UsbMode::HOST ? pll_settings_[6] : 0)
        .WriteTo(phy_mmio);
    PLL_REGISTER::Get(0x34).FromValue(pll_settings_[5]).WriteTo(phy_mmio);
  }

  if (mode == UsbMode::HOST) {
    AddXhciDevice();
    RemoveDwc2Device(std::move(completion));
  } else {
    AddDwc2Device();
    RemoveXhciDevice(std::move(completion));
  }
}

int AmlUsbPhy::IrqThread() {
  auto* mmio = &*usbctrl_mmio_;

  // Wait for PHY to stabilize before reading initial mode.
  zx::nanosleep(zx::deadline_after(zx::sec(1)));

  lock_.Acquire();

  while (true) {
    auto r5 = USB_R5_V2::Get().ReadFrom(mmio);

    // Since |SetMode| is asynchronous, we need to block until it completes.
    sync_completion_t set_mode_sync;
    auto completion = [&](void) { sync_completion_signal(&set_mode_sync); };
    // Read current host/device role.
    if (r5.iddig_curr() == 0) {
      zxlogf(INFO, "Entering USB Host Mode");
      SetMode(UsbMode::HOST, std::move(completion));
    } else {
      zxlogf(INFO, "Entering USB Peripheral Mode");
      SetMode(UsbMode::PERIPHERAL, std::move(completion));
    }

    lock_.Release();
    sync_completion_wait(&set_mode_sync, ZX_TIME_INFINITE);
    auto status = irq_.wait(nullptr);
    if (status == ZX_ERR_CANCELED) {
      return 0;
    } else if (status != ZX_OK) {
      zxlogf(ERROR, "%s: irq_.wait failed: %d", __func__, status);
      return -1;
    }
    lock_.Acquire();

    // Acknowledge interrupt
    r5.ReadFrom(mmio).set_usb_iddig_irq(0).WriteTo(mmio);
  }

  lock_.Release();

  return 0;
}

zx_status_t AmlUsbPhy::Create(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<AmlUsbPhy>(parent);

  auto status = dev->Init();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* _ = dev.release();
  return ZX_OK;
}

zx_status_t AmlUsbPhy::AddXhciDevice() {
  if (xhci_device_) {
    return ZX_ERR_BAD_STATE;
  }

  xhci_device_ = std::make_unique<XhciDevice>(zxdev());

  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_USB_XHCI_COMPOSITE},
  };

  return xhci_device_->DdkAdd(
      ddk::DeviceAddArgs("xhci").set_props(props).set_proto_id(ZX_PROTOCOL_USB_PHY));
}

void AmlUsbPhy::RemoveXhciDevice(SetModeCompletion completion) {
  auto ac = fbl::MakeAutoCall([&]() {
    if (completion)
      completion();
  });
  if (xhci_device_) {
    // The callback will be run by the ChildPreRelease hook once the xhci device has been removed.
    set_mode_completion_ = std::move(completion);
    xhci_device_->DdkAsyncRemove();
  }
}

zx_status_t AmlUsbPhy::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::registers::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void AmlUsbPhy::WriteRegister(uint64_t address, uint32_t value,
                              WriteRegisterCompleter::Sync completer) {
  if (!factory_mmio_.has_value()) {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  constexpr auto kUsbBaseAddress = 0xff400000;
  if ((address < kUsbBaseAddress) || (address >= kUsbBaseAddress + factory_mmio_->get_size())) {
    completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
    return;
  }
  factory_mmio_->Write32(value, address - kUsbBaseAddress);
  completer.ReplySuccess();
}

zx_status_t AmlUsbPhy::AddDwc2Device() {
  if (dwc2_device_) {
    return ZX_ERR_BAD_STATE;
  }

  dwc2_device_ = std::make_unique<Dwc2Device>(zxdev());

  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_USB_DWC2},
  };

  return dwc2_device_->DdkAdd(
      ddk::DeviceAddArgs("dwc2").set_props(props).set_proto_id(ZX_PROTOCOL_USB_PHY));
}

void AmlUsbPhy::RemoveDwc2Device(SetModeCompletion completion) {
  auto ac = fbl::MakeAutoCall([&]() {
    if (completion)
      completion();
  });
  if (dwc2_device_) {
    // The callback will be run by the ChildPreRelease hook once the dwc2 device has been removed.
    set_mode_completion_ = std::move(completion);
    dwc2_device_->DdkAsyncRemove();
  }
}

zx_status_t AmlUsbPhy::Init() {
  if (!pdev_.is_valid()) {
    zxlogf(ERROR, "AmlUsbPhy::Init: could not get platform device protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  size_t actual;
  auto status =
      DdkGetMetadata(DEVICE_METADATA_PRIVATE, pll_settings_, sizeof(pll_settings_), &actual);
  if (status != ZX_OK || actual != sizeof(pll_settings_)) {
    zxlogf(ERROR, "AmlUsbPhy::Init could not get metadata for PLL settings");
    return ZX_ERR_INTERNAL;
  }

  status = pdev_.MapMmio(0, &reset_mmio_);
  if (status != ZX_OK) {
    return status;
  }
  status = pdev_.MapMmio(1, &usbctrl_mmio_);
  if (status != ZX_OK) {
    return status;
  }
  status = pdev_.MapMmio(2, &usbphy20_mmio_);
  if (status != ZX_OK) {
    return status;
  }
  status = pdev_.MapMmio(3, &usbphy21_mmio_);
  if (status != ZX_OK) {
    return status;
  }
  status = pdev_.MapMmio(4, &factory_mmio_);
  if (status != ZX_OK) {
    // Device doesn't support factory mode
    factory_mmio_->reset();
  }

  status = pdev_.GetInterrupt(0, &irq_);
  if (status != ZX_OK) {
    return status;
  }

  status = InitPhy();
  if (status != ZX_OK) {
    return status;
  }
  status = InitOtg();
  if (status != ZX_OK) {
    return status;
  }

  return DdkAdd("aml-usb-phy-v2", DEVICE_ADD_NON_BINDABLE);
}

void AmlUsbPhy::DdkInit(ddk::InitTxn txn) {
  irq_thread_started_ = true;
  int rc = thrd_create_with_name(
      &irq_thread_, [](void* arg) -> int { return reinterpret_cast<AmlUsbPhy*>(arg)->IrqThread(); },
      reinterpret_cast<void*>(this), "amlogic-usb-thread");
  if (rc != thrd_success) {
    irq_thread_started_ = false;
    return txn.Reply(ZX_ERR_INTERNAL);  // This will schedule the device to be unbound.
  }
  return txn.Reply(ZX_OK);
}

// PHY tuning based on connection state
void AmlUsbPhy::UsbPhyConnectStatusChanged(bool connected) {
  fbl::AutoLock lock(&lock_);

  if (dwc2_connected_ == connected)
    return;

  auto* mmio = &*usbphy21_mmio_;

  if (connected) {
    PLL_REGISTER::Get(0x38).FromValue(pll_settings_[7]).WriteTo(mmio);
    PLL_REGISTER::Get(0x34).FromValue(pll_settings_[5]).WriteTo(mmio);
  } else {
    InitPll(mmio);
  }

  dwc2_connected_ = connected;
}

void AmlUsbPhy::DdkUnbindNew(ddk::UnbindTxn txn) {
  irq_.destroy();
  if (irq_thread_started_) {
    thrd_join(irq_thread_, nullptr);
  }
  txn.Reply();
}

void AmlUsbPhy::DdkChildPreRelease(void* child_ctx) {
  fbl::AutoLock lock(&lock_);
  // devmgr will own the device until it is destroyed.
  if (xhci_device_ && (child_ctx == xhci_device_.get())) {
    __UNUSED auto* dev = xhci_device_.release();
  } else if (dwc2_device_ && (child_ctx == dwc2_device_.get())) {
    __UNUSED auto* dev = dwc2_device_.release();
  } else {
    zxlogf(ERROR, "AmlUsbPhy::DdkChildPreRelease unexpected child ctx %p", child_ctx);
  }
  if (set_mode_completion_) {
    // If the mode is currently being set, the irq thread will be blocked
    // until we call this completion.
    set_mode_completion_();
  }
}

void AmlUsbPhy::DdkRelease() { delete this; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlUsbPhy::Create;
  return ops;
}();

}  // namespace aml_usb_phy

ZIRCON_DRIVER_BEGIN(aml_usb_phy, aml_usb_phy::driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AML_USB_PHY_V2), ZIRCON_DRIVER_END(aml_usb_phy)
