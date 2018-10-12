// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-mipi.h"
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

zx_status_t AmlMipiDevice::MipiCsiInit(void* ctx, const mipi_info_t* info) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AmlMipiDevice::MipiCsiDeInit(void* ctx) {
    return ZX_ERR_NOT_SUPPORTED;
}

void AmlMipiDevice::ShutDown() {
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
