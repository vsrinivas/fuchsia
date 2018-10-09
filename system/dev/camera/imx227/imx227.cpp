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

Imx227Device::Imx227Device(zx_device_t* device)
    : ddk::Device<Imx227Device,
                  ddk::Unbindable,
                  ddk::Ioctlable,
                  ddk::Messageable>(device) {
    ddk_proto_id_ = ZX_PROTOCOL_CAMERA;
}

zx_status_t Imx227Device::Init() {
    return ZX_ERR_NOT_SUPPORTED;
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

    zx_status_t status = sensor_device->DdkAdd("imx227");

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
