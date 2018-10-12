// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "imx227.h"
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/camera.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <stdint.h>
#include <threads.h>
#include <zircon/types.h>

namespace camera {

namespace {
constexpr uint16_t kSensorId = 0x0227;
}

Imx227Device::Imx227Device(zx_device_t* device)
    : ddk::Device<Imx227Device,
                  ddk::Unbindable,
                  ddk::Ioctlable,
                  ddk::Messageable>(device) {
    ddk_proto_id_ = ZX_PROTOCOL_CAMERA;
}

zx_status_t Imx227Device::InitPdev(zx_device_t* parent) {
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &pdev_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PLATFORM_DEV not available %d \n", __FUNCTION__, status);
        return status;
    }

    for (uint32_t i = 0; i < countof(gpios_); i++) {
        status = pdev_get_protocol(&pdev_, ZX_PROTOCOL_GPIO, i, &gpios_[i]);
        if (status != ZX_OK) {
            return status;
        }
        // Set the GPIO to output and set initial value to 0.
        ddk::GpioProtocolProxy gpio(&gpios_[i]);
        gpio.ConfigOut(0);
    }

    // I2c for communicating with the sensor.
    status = device_get_protocol(parent, ZX_PROTOCOL_I2C, &i2c_);
    if (status != ZX_OK) {
        return status;
    }

    // Clk for gating clocks for sensor.
    status = device_get_protocol(parent, ZX_PROTOCOL_CLK, &clk_);
    if (status != ZX_OK) {
        return status;
    }

    // Mipi for init and de-init.
    status = device_get_protocol(parent, ZX_PROTOCOL_MIPI_CSI, &mipi_);
    if (status != ZX_OK) {
        return status;
    }

    return ZX_OK;
}

uint8_t Imx227Device::ReadReg(uint16_t addr) {
    // Convert the address to Big Endian format.
    // The camera sensor expects in this format.
    uint16_t buf = htobe16(addr);
    uint8_t val = 0;
    zx_status_t status = i2c_write_read_sync(&i2c_, &buf, sizeof(buf), &val, sizeof(val));
    if (status != ZX_OK) {
        zxlogf(ERROR, "Imx227Device: could not read reg addr: 0x%08x  status: %d\n", addr, status);
        return -1;
    }
    return val;
}

void Imx227Device::WriteReg(uint16_t addr, uint8_t val) {
    // Convert the address to Big Endian format.
    // The camera sensor expects in this format.
    // First two bytes are the address, third one is the value to be written.
    uint8_t buf[3];
    buf[0] = static_cast<uint8_t>(addr & 0xFF);
    buf[1] = static_cast<uint8_t>((addr >> 8) & 0xFF);
    buf[2] = val;

    zx_status_t status = i2c_write_sync(&i2c_, buf, 3);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Imx227Device: could not write reg addr/val: 0x%08x/0x%08x status: %d\n",
               addr, val, status);
    }
}

bool Imx227Device::ValidateSensorID() {
    uint16_t sensor_id = static_cast<uint16_t>((ReadReg(0x0016) << 8) | ReadReg(0x0017));
    if (sensor_id != kSensorId) {
        zxlogf(ERROR, "Imx227Device: Invalid sensor ID\n");
        return false;
    }
    return true;
}

zx_status_t Imx227Device::Init() {

    // Power up sequence. Reference: Page 51- IMX227-0AQH5-C datasheet.
    ddk::GpioProtocolProxy gpio_vana(&gpios_[VANA_ENABLE]);
    gpio_vana.ConfigOut(1);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

    ddk::GpioProtocolProxy gpio_vdig(&gpios_[VDIG_ENABLE]);
    gpio_vdig.ConfigOut(1);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

    // Enable 24M clock for sensor.
    ddk::ClkProtocolProxy clk(&clk_);
    clk.Enable(0);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

    ddk::GpioProtocolProxy gpio_rst(&gpios_[CAM_SENSOR_RST]);
    gpio_rst.ConfigOut(0);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

    // Get Sensor ID to validate initialization sequence.
    if (!ValidateSensorID()) {
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

void Imx227Device::DeInit() {
}

zx_status_t Imx227Device::GetInfo(zircon_camera_SensorInfo* out_info) {
    return ZX_ERR_NOT_SUPPORTED;
}

void Imx227Device::SetMode(uint8_t mode) {
}

void Imx227Device::StartStreaming() {
}

void Imx227Device::StopStreaming() {
}

int32_t Imx227Device::AllocateAnalogGain(int32_t gain) {
    return ZX_ERR_NOT_SUPPORTED;
}

int32_t Imx227Device::AllocateDigitalGain(int32_t gain) {
    return ZX_ERR_NOT_SUPPORTED;
}

void Imx227Device::Update() {
}

static zx_status_t Init(void* ctx, fidl_txn_t* txn) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    zx_status_t status = self.Init();
    return zircon_camera_CameraSensorInit_reply(txn, status);
}

static zx_status_t DeInit(void* ctx) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    self.DeInit();
    return ZX_OK;
}

static zx_status_t SetMode(void* ctx, uint8_t mode) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    self.SetMode(mode);
    return ZX_OK;
}

static zx_status_t StartStreaming(void* ctx) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    self.StartStreaming();
    return ZX_OK;
}

static zx_status_t StopStreaming(void* ctx) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    self.StopStreaming();
    return ZX_OK;
}

static zx_status_t AllocateAnalogGain(void* ctx, int32_t gain, fidl_txn_t* txn) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    int32_t actual_gain = self.AllocateAnalogGain(gain);
    return zircon_camera_CameraSensorAllocateAnalogGain_reply(txn, actual_gain);
}

static zx_status_t AllocateDigitalGain(void* ctx, int32_t gain, fidl_txn_t* txn) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    int32_t actual_gain = self.AllocateDigitalGain(gain);
    return zircon_camera_CameraSensorAllocateDigitalGain_reply(txn, actual_gain);
}

static zx_status_t Update(void* ctx) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    self.Update();
    return ZX_OK;
}

zircon_camera_CameraSensor_ops_t fidl_ops = {
    .Init = Init,
    .DeInit = DeInit,
    .SetMode = SetMode,
    .StartStreaming = StartStreaming,
    .StopStreaming = StopStreaming,
    .AllocateAnalogGain = AllocateAnalogGain,
    .AllocateDigitalGain = AllocateDigitalGain,
    .Update = Update,
};

zx_status_t Imx227Device::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return zircon_camera_CameraSensor_dispatch(this, txn, msg, &fidl_ops);
}

zx_status_t Imx227Device::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                                   void* out_buf, size_t out_len, size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::Create(zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto sensor_device = fbl::make_unique_checked<Imx227Device>(&ac, parent);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = sensor_device->InitPdev(parent);
    if (status != ZX_OK) {
        return status;
    }

    status = sensor_device->DdkAdd("imx227");

    // sensor_device intentionally leaked as it is now held by DevMgr.
    __UNUSED auto ptr = sensor_device.release();

    return status;
}

void Imx227Device::ShutDown() {
}

void Imx227Device::DdkUnbind() {
    DdkRemove();
}

void Imx227Device::DdkRelease() {
    ShutDown();
    delete this;
}

} // namespace camera

extern "C" zx_status_t imx227_bind(void* ctx, zx_device_t* device) {
    return camera::Imx227Device::Create(device);
}
