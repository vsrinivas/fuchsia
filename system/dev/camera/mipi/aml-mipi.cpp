// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-mipi.h"
#include "aml-mipi-regs.h"
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/camera.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <stdint.h>
#include <threads.h>
#include <zircon/types.h>

namespace camera {

namespace {

// MMIO Indexes.
constexpr uint32_t kCsiPhy0 = 0;
constexpr uint32_t kAphy0 = 1;
constexpr uint32_t kCsiHost0 = 2;
constexpr uint32_t kMipiAdap = 3;

} // namespace

zx_status_t AmlMipiDevice::InitPdev(zx_device_t* parent) {
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &pdev_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PLATFORM_DEV not available %d \n", __FUNCTION__, status);
        return status;
    }

    mmio_buffer_t mmio;
    status = pdev_map_mmio_buffer2(&pdev_,
                                   kCsiPhy0,
                                   ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                   &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer2 failed %d\n", __FUNCTION__, status);
        return status;
    }
    csi_phy0_mmio_ = fbl::make_unique<ddk::MmioBuffer>(mmio);

    status = pdev_map_mmio_buffer2(&pdev_, kAphy0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer2 failed %d\n", __FUNCTION__, status);
        return status;
    }
    aphy0_mmio_ = fbl::make_unique<ddk::MmioBuffer>(mmio);

    status = pdev_map_mmio_buffer2(&pdev_, kCsiHost0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer2 failed %d\n", __FUNCTION__, status);
        return status;
    }
    csi_host0_mmio_ = fbl::make_unique<ddk::MmioBuffer>(mmio);

    status = pdev_map_mmio_buffer2(&pdev_, kMipiAdap, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer2 failed %d\n", __FUNCTION__, status);
        return status;
    }
    mipi_adap_mmio_ = fbl::make_unique<ddk::MmioBuffer>(mmio);

    return status;
}

void AmlMipiDevice::MipiPhyReset() {
    uint32_t data32 = 0x1f; //disable lanes digital clock
    data32 |= 0x1 << 31;    //soft reset bit
    csi_phy0_mmio_->Write32(data32, MIPI_PHY_CTRL);
}

void AmlMipiDevice::MipiCsi2Reset() {
    csi_host0_mmio_->Write32(0, MIPI_CSI_PHY_SHUTDOWNZ); // enable power
    csi_host0_mmio_->Write32(0, MIPI_CSI_DPHY_RSTZ);     // release DPHY reset
    csi_host0_mmio_->Write32(0, MIPI_CSI_CSI2_RESETN);   // csi2 reset
}

void AmlMipiDevice::MipiPhyInit(const mipi_info_t* info) {
    if (info->ui_value <= 1) {
        aphy0_mmio_->Write32(0x0b440585, HI_CSI_PHY_CNTL0);
    } else {
        aphy0_mmio_->Write32(0x0b440581, HI_CSI_PHY_CNTL0);
    }

    aphy0_mmio_->Write32(0x803f0000, HI_CSI_PHY_CNTL1);
    aphy0_mmio_->Write32(0x02, HI_CSI_PHY_CNTL3);

    // 3d8 :continue mode
    csi_phy0_mmio_->Write32(0x3d8, MIPI_PHY_CLK_LANE_CTRL);
    // clck miss = 50 ns --(x< 60 ns)
    csi_phy0_mmio_->Write32(0x9, MIPI_PHY_TCLK_MISS);
    // clck settle = 160 ns --(95ns< x < 300 ns)
    csi_phy0_mmio_->Write32(0x1f, MIPI_PHY_TCLK_SETTLE);
    // hs exit = 160 ns --(x>100ns)
    csi_phy0_mmio_->Write32(0x1f, MIPI_PHY_THS_EXIT);
    // hs skip = 55 ns --(40ns<x<55ns+4*UI)
    csi_phy0_mmio_->Write32(0xa, MIPI_PHY_THS_SKIP);

    // No documentation for this regisgter.
    // hs settle = 160 ns --(85 ns + 6*UI<x<145 ns + 10*UI)
    uint32_t settle = ((85 + 145 + (16 * info->ui_value)) / 2) / 5;
    csi_phy0_mmio_->Write32(settle, MIPI_PHY_THS_SETTLE);

    csi_phy0_mmio_->Write32(0x4e20, MIPI_PHY_TINIT); // >100us
    csi_phy0_mmio_->Write32(0x100, MIPI_PHY_TMBIAS);
    csi_phy0_mmio_->Write32(0x1000, MIPI_PHY_TULPS_C);
    csi_phy0_mmio_->Write32(0x100, MIPI_PHY_TULPS_S);
    csi_phy0_mmio_->Write32(0x0c, MIPI_PHY_TLP_EN_W);
    csi_phy0_mmio_->Write32(0x100, MIPI_PHY_TLPOK);
    csi_phy0_mmio_->Write32(0x400000, MIPI_PHY_TWD_INIT);
    csi_phy0_mmio_->Write32(0x400000, MIPI_PHY_TWD_HS);
    csi_phy0_mmio_->Write32(0x0, MIPI_PHY_DATA_LANE_CTRL);
    // enable data lanes pipe line and hs sync bit err.
    csi_phy0_mmio_->Write32((0x3 | (0x1f << 2) | (0x3 << 7)), MIPI_PHY_DATA_LANE_CTRL1);
    csi_phy0_mmio_->Write32(0x00000123, MIPI_PHY_MUX_CTRL0);
    csi_phy0_mmio_->Write32(0x00000123, MIPI_PHY_MUX_CTRL1);

    // NOTE: Possible bug in reference code. Leaving it here for future reference.
    // data32 = ((~(m_info->channel)) & 0xf) | (0 << 4); //enable lanes digital clock
    // data32 |= ((0x10 | m_info->channel) << 5);        //mipi_chpu  to analog
    csi_phy0_mmio_->Write32(0, MIPI_PHY_CTRL);
}

void AmlMipiDevice::MipiCsi2Init(const mipi_info_t* info) {
    // csi2 reset
    csi_host0_mmio_->Write32(MIPI_CSI_CSI2_RESETN, 0);
    // release csi2 reset
    csi_host0_mmio_->Write32(MIPI_CSI_CSI2_RESETN, 0xffffffff);
    // release DPHY reset
    csi_host0_mmio_->Write32(MIPI_CSI_DPHY_RSTZ, 0xffffffff);
    //set lanes
    csi_host0_mmio_->Write32(MIPI_CSI_N_LANES, (info->lanes - 1) & 3);
    // enable power
    csi_host0_mmio_->Write32(MIPI_CSI_PHY_SHUTDOWNZ, 0xffffffff);
}

zx_status_t AmlMipiDevice::MipiCsiInit(void* ctx,
                                       const mipi_info_t* mipi_info,
                                       const mipi_adap_info_t* adap_info) {
    auto& self = *static_cast<AmlMipiDevice*>(ctx);

    // Initialize the PHY.
    self.MipiPhyInit(mipi_info);
    // Initialize the CSI Host.
    self.MipiCsi2Init(mipi_info);

    // Initialize the MIPI Adapter.
    zx_status_t status = self.MipiAdapInit(adap_info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: MipiAdapInit failed %d\n", __FUNCTION__, status);
        return status;
    }

    // Start the MIPI Adapter.
    self.MipiAdapStart();
    return status;
}

zx_status_t AmlMipiDevice::MipiCsiDeInit(void* ctx) {
    auto& self = *static_cast<AmlMipiDevice*>(ctx);
    self.MipiPhyReset();
    self.MipiCsi2Reset();
    self.MipiAdapReset();
    return ZX_OK;
}

void AmlMipiDevice::ShutDown() {
    MipiCsiDeInit(this);
    csi_phy0_mmio_.reset();
    aphy0_mmio_.reset();
    csi_host0_mmio_.reset();
    mipi_adap_mmio_.reset();
}

static void DdkUnbind(void* ctx) {
    auto& self = *static_cast<AmlMipiDevice*>(ctx);
    device_remove(self.device_);
}

static void DdkRelease(void* ctx) {
    auto& self = *static_cast<AmlMipiDevice*>(ctx);
    self.ShutDown();
    delete &self;
}

static mipi_csi_protocol_ops_t proto_ops = {
    .init = AmlMipiDevice::MipiCsiInit,
    .de_init = AmlMipiDevice::MipiCsiDeInit,
};

static zx_protocol_device_t mipi_device_ops = []() {
    zx_protocol_device_t result;

    result.version = DEVICE_OPS_VERSION;
    result.unbind = &DdkUnbind;
    result.release = &DdkRelease;
    return result;
}();

static device_add_args_t mipi_dev_args = []() {
    device_add_args_t result;

    result.version = DEVICE_ADD_ARGS_VERSION;
    result.name = "aml-mipi";
    result.ops = &mipi_device_ops;
    result.proto_id = ZX_PROTOCOL_MIPI_CSI;
    result.proto_ops = &proto_ops;
    return result;
}();

zx_status_t AmlMipiDevice::Create(zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto mipi_device = fbl::make_unique_checked<AmlMipiDevice>(&ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = mipi_device->InitPdev(parent);
    if (status != ZX_OK) {
        return status;
    }
    // Populate board specific information
    camera_sensor_t sensor_info;
    size_t actual;
    status = device_get_metadata(parent, DEVICE_METADATA_PRIVATE, &sensor_info,
                                 sizeof(camera_sensor_t), &actual);
    if (status != ZX_OK || actual != sizeof(camera_sensor_t)) {
        zxlogf(ERROR, "aml-mipi: Could not get Sensor Info metadata %d\n", status);
        return status;
    }

    static zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, sensor_info.vid},
        {BIND_PLATFORM_DEV_PID, 0, sensor_info.pid},
        {BIND_PLATFORM_DEV_DID, 0, sensor_info.did},
    };

    mipi_dev_args.props = props;
    mipi_dev_args.prop_count = countof(props);
    mipi_dev_args.ctx = mipi_device.get();

    status = pdev_device_add(&mipi_device->pdev_, 0, &mipi_dev_args, &mipi_device->device_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-mipi driver failed to get added\n");
        return status;
    } else {
        zxlogf(INFO, "aml-mipi driver added\n");
    }

    // mipi_device intentionally leaked as it is now held by DevMgr.
    __UNUSED auto ptr = mipi_device.release();

    return status;
}

} // namespace camera

extern "C" zx_status_t aml_mipi_bind(void* ctx, zx_device_t* device) {
    return camera::AmlMipiDevice::Create(device);
}
