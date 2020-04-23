// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nelson-usb-phy.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <hw/reg.h>
#include <soc/aml-common/aml-g12-reset.h>

#include "usb-phy-regs.h"

namespace nelson_usb_phy {

constexpr auto kStabilizeTime = zx::sec(1);

void NelsonUsbPhy::InitPll(ddk::MmioBuffer* mmio) {
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

zx_status_t NelsonUsbPhy::InitPhy() {
  auto& reset_mmio = *reset_mmio_;
  auto& usbctrl_mmio = *usbctrl_mmio_;
  auto& power_mmio = *power_mmio_;

  // Do stuff necessary to turn on the power to USB
  if (power_mmio_.has_value()) {
    A0_RTI_GEN_PWR_SLEEP0::Get()
        .ReadFrom(&sleep_mmio_.value())
        .set_usb_comb_power_off(0)
        .WriteTo(&sleep_mmio_.value());
    UNKNOWN_REGISTER::Get().ReadFrom(&power_mmio).set_unknown_30(0).WriteTo(&power_mmio);
    UNKNOWN_REGISTER::Get().ReadFrom(&power_mmio).set_unknown_31(0).WriteTo(&power_mmio);
    zx::nanosleep(zx::deadline_after(zx::usec(100)));

    UNKNOWN_REGISTER1::Get().ReadFrom(&reset_mmio).set_unknown_2(0).WriteTo(&reset_mmio);
    zx::nanosleep(zx::deadline_after(zx::usec(100)));
    A0_RTI_GEN_PWR_ISO0::Get()
        .ReadFrom(&sleep_mmio_.value())
        .set_usb_comb_isolation_enable(0)
        .WriteTo(&sleep_mmio_.value());

    UNKNOWN_REGISTER1::Get().ReadFrom(&reset_mmio).set_unknown_2(1).WriteTo(&reset_mmio);
    zx::nanosleep(zx::deadline_after(zx::usec(100)));
    A0_RTI_GEN_PWR_SLEEP0::Get()
        .ReadFrom(&sleep_mmio_.value())
        .set_pci_comb_power_off(0)
        .WriteTo(&sleep_mmio_.value());

    auto unknown_tmp_1 = UNKNOWN_REGISTER1::Get().ReadFrom(&reset_mmio);
    unknown_tmp_1.set_unknown_26(0);
    unknown_tmp_1.set_unknown_27(0);
    unknown_tmp_1.set_unknown_28(0);
    unknown_tmp_1.set_unknown_29(0);
    unknown_tmp_1.WriteTo(&reset_mmio);

    A0_RTI_GEN_PWR_ISO0::Get()
        .ReadFrom(&sleep_mmio_.value())
        .set_pci_comb_isolation_enable(0)
        .WriteTo(&sleep_mmio_.value());
    A0_RTI_GEN_PWR_SLEEP0::Get()
        .ReadFrom(&sleep_mmio_.value())
        .set_ge2d_power_off(0)
        .WriteTo(&sleep_mmio_.value());

    auto unknown_tmp = UNKNOWN_REGISTER::Get().ReadFrom(&power_mmio);
    unknown_tmp.set_unknown_18(0);
    unknown_tmp.set_unknown_19(0);
    unknown_tmp.set_unknown_20(0);
    unknown_tmp.set_unknown_21(0);
    unknown_tmp.set_unknown_22(0);
    unknown_tmp.set_unknown_23(0);
    unknown_tmp.set_unknown_24(0);
    unknown_tmp.set_unknown_25(0);
    unknown_tmp_1.WriteTo(&power_mmio);

    A0_RTI_GEN_PWR_ISO0::Get()
        .ReadFrom(&sleep_mmio_.value())
        .set_ge2d_isolation_enable(0)
        .WriteTo(&sleep_mmio_.value());
    A0_RTI_GEN_PWR_ISO0::Get()
        .ReadFrom(&sleep_mmio_.value())
        .set_ge2d_isolation_enable(1)
        .WriteTo(&sleep_mmio_.value());

    unknown_tmp = UNKNOWN_REGISTER::Get().ReadFrom(&power_mmio);
    unknown_tmp.set_unknown_18(1);
    unknown_tmp.set_unknown_19(1);
    unknown_tmp.set_unknown_20(1);
    unknown_tmp.set_unknown_21(1);
    unknown_tmp.set_unknown_22(1);
    unknown_tmp.set_unknown_23(1);
    unknown_tmp.set_unknown_24(1);
    unknown_tmp.set_unknown_25(1);
    unknown_tmp_1.WriteTo(&power_mmio);
    A0_RTI_GEN_PWR_SLEEP0::Get()
        .ReadFrom(&sleep_mmio_.value())
        .set_ge2d_power_off(1)
        .WriteTo(&sleep_mmio_.value());
  }

  // first reset USB
  auto reset_1_level = aml_reset::RESET_1::GetLevel().ReadFrom(&reset_mmio);
  reset_1_level.set_unknown_field_a(1);
  reset_1_level.set_unknown_field_b(1);
  reset_1_level.WriteTo(&reset_mmio);

  auto reset_1 = aml_reset::RESET_1::Get().ReadFrom(&reset_mmio);
  reset_1.set_usb(1);
  reset_1.WriteTo(&reset_mmio);
  zx::nanosleep(zx::deadline_after(zx::usec(500)));
  for (int i = 0; i < 2; i++) {
    auto u2p_r0 = U2P_R0_V2::Get(i).ReadFrom(&usbctrl_mmio);
    u2p_r0.set_por(1);
    u2p_r0.set_host_device(1);
    if (i == 1) {
      u2p_r0.set_idpullup0(1);
      u2p_r0.set_drvvbus0(1);
    }
    u2p_r0.WriteTo(&usbctrl_mmio);

    zx::nanosleep(zx::deadline_after(zx::usec(10)));

    reset_1.ReadFrom(&reset_mmio);
    reset_1.set_unknown_field_a(1);
    reset_1.WriteTo(&reset_mmio);
    zx::nanosleep(zx::deadline_after(zx::usec(50)));

    auto u2p_r1 = U2P_R1_V2::Get(i);

    int count = 0;
    while (!u2p_r1.ReadFrom(&usbctrl_mmio).phy_rdy()) {
      // wait phy ready max 1ms, common is 100us
      if (count > 200) {
        zxlogf(ERROR, "NelsonUsbPhy::InitPhy U2P_R1_PHY_RDY wait failed");
        break;
      }
      count++;
      zx::nanosleep(zx::deadline_after(zx::usec(5)));
    }
  }

  return ZX_OK;
}

zx_status_t NelsonUsbPhy::InitOtg() {
  auto* mmio = &*usbctrl_mmio_;

  USB_R1_V2::Get().ReadFrom(mmio).set_u3h_fladj_30mhz_reg(0x20).WriteTo(mmio);

  USB_R5_V2::Get().ReadFrom(mmio).set_iddig_en0(1).set_iddig_en1(1).set_iddig_th(255).WriteTo(mmio);

  return ZX_OK;
}

void NelsonUsbPhy::SetMode(UsbMode mode) {
  ZX_DEBUG_ASSERT(mode == UsbMode::HOST || mode == UsbMode::PERIPHERAL);
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
    RemoveDwc2Device(true);
    AddXhciDevice();
  } else {
    RemoveXhciDevice(true);
    AddDwc2Device();
  }
}

int NelsonUsbPhy::IrqThread() {
  auto* mmio = &*usbctrl_mmio_;

  // Wait for PHY to stabilize before reading initial mode.
  zx::nanosleep(zx::deadline_after(kStabilizeTime));

  lock_.Acquire();

  while (true) {
    auto r5 = USB_R5_V2::Get().ReadFrom(mmio);

    // Read current host/device role.
    if (r5.iddig_curr() == 0) {
      zxlogf(INFO, "Entering USB Host Mode");
      SetMode(UsbMode::HOST);
    } else {
      zxlogf(INFO, "Entering USB Peripheral Mode");
      SetMode(UsbMode::PERIPHERAL);
    }

    lock_.Release();
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

zx_status_t NelsonUsbPhy::Create(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<NelsonUsbPhy>(parent);

  auto status = dev->Init();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* _ = dev.release();
  return ZX_OK;
}

zx_status_t NelsonUsbPhy::AddXhciDevice() {
  if (xhci_device_) {
    return ZX_ERR_BAD_STATE;
  }

  xhci_device_ = std::make_unique<XhciDevice>(zxdev());

  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_USB_XHCI_COMPOSITE},
  };
  xhci_device_->DdkAdd("xhci", 0, props, countof(props), ZX_PROTOCOL_USB_PHY);
  return ZX_OK;
}

void NelsonUsbPhy::RemoveXhciDevice(bool wait) {
  if (xhci_device_) {
    // devmgr will own the device until it is destroyed.
    auto* dev = xhci_device_.release();
    sync_completion_reset(&remove_event_);
    dev->DdkAsyncRemove();
    if (wait) {
      sync_completion_wait(&remove_event_, ZX_TIME_INFINITE);
    }
  }
}

// Support for USB OTG
// We need this to ensure that our children have
// unbound before we mode switch.
void NelsonUsbPhy::DdkChildPreRelease(void* ctx) {
  if ((ctx == xhci_device_.get()) || (ctx == dwc2_device_.get())) {
    UsbPhyNotifyDeviceRemoved();
  }
}

zx_status_t NelsonUsbPhy::UsbPhyNotifyDeviceRemoved() {
  sync_completion_signal(&remove_event_);
  return ZX_OK;
}

zx_status_t NelsonUsbPhy::AddDwc2Device() {
  if (dwc2_device_) {
    return ZX_ERR_BAD_STATE;
  }

  dwc2_device_ = std::make_unique<Dwc2Device>(zxdev());

  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_USB_DWC2},
  };

  return dwc2_device_->DdkAdd("dwc2", 0, props, countof(props), ZX_PROTOCOL_USB_PHY);
}

void NelsonUsbPhy::RemoveDwc2Device(bool wait) {
  if (dwc2_device_) {
    // devmgr will own the device until it is destroyed.
    auto* dev = dwc2_device_.release();
    sync_completion_reset(&remove_event_);
    dev->DdkAsyncRemove();
    if (wait) {
      sync_completion_wait(&remove_event_, ZX_TIME_INFINITE);
    }
  }
}

zx_status_t NelsonUsbPhy::Init() {
  if (!pdev_.is_valid()) {
    zxlogf(ERROR, "NelsonUsbPhy::Init: could not get platform device protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }
  size_t actual;
  auto status =
      DdkGetMetadata(DEVICE_METADATA_PRIVATE, pll_settings_, sizeof(pll_settings_), &actual);
  if (status != ZX_OK || actual != sizeof(pll_settings_)) {
    zxlogf(ERROR, "NelsonUsbPhy::Init could not get metadata for PLL settings");
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

  status = pdev_.MapMmio(4, &power_mmio_);
  if (status != ZX_OK) {
    return status;
  }

  status = pdev_.MapMmio(5, &sleep_mmio_);
  if (status != ZX_OK) {
    return status;
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

  status = DdkAdd("nelson-usb-phy", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    return status;
  }

  int rc = thrd_create_with_name(
      &irq_thread_,
      [](void* arg) -> int { return reinterpret_cast<NelsonUsbPhy*>(arg)->IrqThread(); },
      reinterpret_cast<void*>(this), "nelson-usb-thread");
  if (rc != thrd_success) {
    DdkAsyncRemove();
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

// PHY tuning based on connection state
void NelsonUsbPhy::UsbPhyConnectStatusChanged(bool connected) {
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
  return;
}

void NelsonUsbPhy::DdkUnbindNew(ddk::UnbindTxn txn) {
  irq_.destroy();
  thrd_join(irq_thread_, nullptr);

  RemoveXhciDevice(false);
  RemoveDwc2Device(false);
  txn.Reply();
}

void NelsonUsbPhy::DdkRelease() { delete this; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = NelsonUsbPhy::Create;
  return ops;
}();

}  // namespace nelson_usb_phy

ZIRCON_DRIVER_BEGIN(nelson_usb_phy, nelson_usb_phy::driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_NELSON_USB_PHY),
    ZIRCON_DRIVER_END(nelson_usb_phy)
