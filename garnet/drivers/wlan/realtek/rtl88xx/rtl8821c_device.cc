// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "bus_util.h"
#include "rtl8821c_device.h"
#include "rtl88xx_registers.h"

namespace wlan {
namespace rtl88xx {

Rtl8821cDevice::Rtl8821cDevice() {}

Rtl8821cDevice::~Rtl8821cDevice() {}

// static
zx_status_t Rtl8821cDevice::Create(std::unique_ptr<Bus> bus, std::unique_ptr<Device>* device) {
    std::unique_ptr<Rtl8821cDevice> rtl8821c_device(new Rtl8821cDevice());
    rtl8821c_device->bus_ = std::move(bus);

    zx_status_t status = ZX_OK;
    if ((status = rtl8821c_device->PreInitSystemCfg88xx()) != ZX_OK) { return status; }

    *device = std::move(rtl8821c_device);
    return ZX_OK;
}

zx_status_t Rtl8821cDevice::CreateWlanMac(zx_device_t* parent_device, WlanMac** wlan_mac) {
    zxlogf(ERROR, "rtl88xx: Rtl8821cDevice::CreateWlanMac() not implemented\n");
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Rtl8821cDevice::PreInitSystemCfg88xx() {
    zx_status_t status = ZX_OK;

    if ((status = UpdateRegisters(bus_.get(), [](reg::RSV_CTRL* rsv_ctrl) {
             rsv_ctrl->set_wlock_all(0);
             rsv_ctrl->set_wlock_00(0);
             rsv_ctrl->set_wlock_04(0);
             rsv_ctrl->set_wlock_08(0);
             rsv_ctrl->set_wlock_40(0);
             rsv_ctrl->set_wlock_1c_b6(0);
             rsv_ctrl->set_r_dis_prst(0);
             rsv_ctrl->set_lock_all_en(0);
         })) != ZX_OK) {
        return status;
    }

    // A few bus-specific register configurations to set.
    switch (bus_->GetBusType()) {
    case Bus::BusType::kUsb: {
        if ((status = UpdateRegisters(bus_.get(), [](reg::USB_DMA_AGG_TO* usb_dma_agg_to) {
                 usb_dma_agg_to->set_bit_4(1);
             })) != ZX_OK) {
            return status;
        }
        break;
    }
    default:
        break;
    }

    if ((status = UpdateRegisters(bus_.get(), [](reg::PAD_CTRL1* pad_ctrl1, reg::LED_CFG* led_cfg,
                                                 reg::GPIO_MUXCFG* gpio_muxcfg) {
             pad_ctrl1->set_lnaon_wlbt_sel(1);
             pad_ctrl1->set_pape_wlbt_sel(1);
             led_cfg->set_pape_sel_en(0);
             led_cfg->set_lnaon_sel_en(0);
             gpio_muxcfg->set_wlrfe_4_5_en(1);
         })) != ZX_OK) {
        return status;
    }

    // Turn off the RF output while configuring the chip.
    if ((status = UpdateRegisters(bus_.get(), [](reg::SYS_FUNC_EN* sys_func_en,
                                                 reg::RF_CTRL* rf_ctrl, reg::WLRF1* wlrf1) {
             sys_func_en->set_fen_bbrstb(0);
             sys_func_en->set_fen_bb_glb_rstn(0);
             rf_ctrl->set_rf_en(0);
             rf_ctrl->set_rf_rstb(0);
             rf_ctrl->set_rf_sdmrstb(0);
             wlrf1->set_wlrf1_ctrl(wlrf1->wlrf1_ctrl() & ~0x03u);
         })) != ZX_OK) {
        return status;
    }

    return ZX_OK;
}

}  // namespace rtl88xx
}  // namespace wlan
