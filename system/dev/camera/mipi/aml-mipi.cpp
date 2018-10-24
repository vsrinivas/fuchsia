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

// NOTE: A lot of magic numbers, they come from vendor
//       source code.

namespace camera {

namespace {

// MMIO Indexes.
constexpr uint32_t kCsiPhy0 = 0;
constexpr uint32_t kAphy0 = 1;
constexpr uint32_t kCsiHost0 = 2;
constexpr uint32_t kMipiAdap = 3;
constexpr uint32_t kHiu = 4;
constexpr uint32_t kPowerDomain = 5;
constexpr uint32_t kMemoryDomain = 6;
constexpr uint32_t kReset = 7;

// CLK Shifts & Masks
constexpr uint32_t kClkMuxMask = 0xfff;
constexpr uint32_t kClkEnableShift = 8;

} // namespace

void AmlMipiDevice::IspHWReset(bool reset) {
    if (reset) {
        reset_mmio_->ClearBits32(1 << 1, RESET4_LEVEL);
    } else {
        reset_mmio_->SetBits32(1 << 1, RESET4_LEVEL);
    }
}

void AmlMipiDevice::PowerUpIsp() {
    // set bit[18-19]=0
    power_mmio_->ClearBits32(1 << 18 | 1 << 19, AO_RTI_GEN_PWR_SLEEP0);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));

    // set bit[18-19]=0
    power_mmio_->ClearBits32(1 << 18 | 1 << 19, AO_RTI_GEN_PWR_ISO0);

    // MEM_PD_REG0 set 0
    memory_pd_mmio_->Write32(0, HHI_ISP_MEM_PD_REG0);
    // MEM_PD_REG1 set 0
    memory_pd_mmio_->Write32(0, HHI_ISP_MEM_PD_REG1);

    hiu_mmio_->Write32(0x5b446585, HHI_CSI_PHY_CNTL0);
    hiu_mmio_->Write32(0x803f4321, HHI_CSI_PHY_CNTL1);
}

void AmlMipiDevice::InitMipiClock() {
    // clear existing values
    hiu_mmio_->ClearBits32(kClkMuxMask, HHI_MIPI_ISP_CLK_CNTL);
    // set the divisor = 1 (writing (1-1) to div field)
    // source for the unused mux = S905D2_FCLK_DIV3   = 3 // 666.7 MHz
    hiu_mmio_->SetBits32(((1 << kClkEnableShift) | 4 << 9),
                         HHI_MIPI_ISP_CLK_CNTL);

    // clear existing values
    hiu_mmio_->ClearBits32(kClkMuxMask, HHI_MIPI_CSI_PHY_CLK_CNTL);
    // set the divisor = 2 (writing (2-1) to div field)
    // source for the unused mux = S905D2_FCLK_DIV5   = 6 // 400 MHz
    hiu_mmio_->SetBits32(((1 << kClkEnableShift) | 6 << 9 | 1),
                         HHI_MIPI_CSI_PHY_CLK_CNTL);

    zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
}

zx_status_t AmlMipiDevice::InitPdev(zx_device_t* parent) {
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available %d \n", __FUNCTION__, status);
        return status;
    }

    mmio_buffer_t mmio;
    status = pdev_map_mmio_buffer2(&pdev_,
                                   kCsiPhy0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
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

    status = pdev_map_mmio_buffer2(&pdev_, kHiu, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer2 failed %d\n", __FUNCTION__, status);
        return status;
    }
    hiu_mmio_ = fbl::make_unique<ddk::MmioBuffer>(mmio);

    status = pdev_map_mmio_buffer2(&pdev_, kPowerDomain, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer2 failed %d\n", __FUNCTION__, status);
        return status;
    }
    power_mmio_ = fbl::make_unique<ddk::MmioBuffer>(mmio);

    status = pdev_map_mmio_buffer2(&pdev_, kMemoryDomain, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer2 failed %d\n", __FUNCTION__, status);
        return status;
    }
    memory_pd_mmio_ = fbl::make_unique<ddk::MmioBuffer>(mmio);

    status = pdev_map_mmio_buffer2(&pdev_, kReset, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer2 failed %d\n", __FUNCTION__, status);
        return status;
    }
    reset_mmio_ = fbl::make_unique<ddk::MmioBuffer>(mmio);

    // Get our bti.
    status = pdev_get_bti(&pdev_, 0, bti_.reset_and_get_address());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not obtain bti: %d\n", __FUNCTION__, status);
        return status;
    }

    // Get adapter interrupt.
    status = pdev_map_interrupt(&pdev_, 0, adap_irq_.reset_and_get_address());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not obtain adapter interrupt %d\n", __FUNCTION__, status);
        return status;
    }

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
    // uint32_t data32 = ((~(info->channel)) & 0xf) | (0 << 4); //enable lanes digital clock
    // data32 |= ((0x10 | info->channel) << 5);        //mipi_chpu  to analog
    csi_phy0_mmio_->Write32(0, MIPI_PHY_CTRL);
}

void AmlMipiDevice::MipiCsi2Init(const mipi_info_t* info) {
    // csi2 reset
    csi_host0_mmio_->Write32(0, MIPI_CSI_CSI2_RESETN);
    // release csi2 reset
    csi_host0_mmio_->Write32(0xffffffff, MIPI_CSI_CSI2_RESETN);
    // release DPHY reset
    csi_host0_mmio_->Write32(0xffffffff, MIPI_CSI_DPHY_RSTZ);
    //set lanes
    csi_host0_mmio_->Write32((info->lanes - 1) & 3, MIPI_CSI_N_LANES);
    // enable power
    csi_host0_mmio_->Write32(0xffffffff, MIPI_CSI_PHY_SHUTDOWNZ);
}

// static
zx_status_t AmlMipiDevice::MipiCsiInit(void* ctx,
                                       const mipi_info_t* mipi_info,
                                       const mipi_adap_info_t* adap_info) {
    auto& self = *static_cast<AmlMipiDevice*>(ctx);

    // The ISP and MIPI module is in same power domain.
    // So if we don't call the power sequence of ISP, the mipi module
    // won't work and it will block accesses to the  mipi register block.
    self.PowerUpIsp();

    // Setup MIPI CSI PHY CLK to 200MHz.
    // Setup MIPI ISP CLK to 667MHz.
    self.InitMipiClock();

    self.IspHWReset(true);
    self.IspHWReset(false);

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
    self.MipiAdapStart(adap_info);
    return status;
}

// static
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
    hiu_mmio_.reset();
    power_mmio_.reset();
    memory_pd_mmio_.reset();
    reset_mmio_.reset();
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

// static
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

AmlMipiDevice::~AmlMipiDevice() {
    adap_irq_.destroy();
    running_.store(false);
    thrd_join(irq_thread_, NULL);
}

} // namespace camera

extern "C" zx_status_t aml_mipi_bind(void* ctx, zx_device_t* device) {
    return camera::AmlMipiDevice::Create(device);
}
