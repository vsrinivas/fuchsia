// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>
#include <ddktl/device.h>
#include <zircon/camera/c/fidl.h>

namespace camera {

class Imx227Device;
using DeviceType = ddk::Device<Imx227Device,
                               ddk::Unbindable,
                               ddk::Ioctlable,
                               ddk::Messageable>;

class Imx227Device : public DeviceType,
                     public ddk::internal::base_protocol {
public:
    static zx_status_t Create(zx_device_t* parent);
    Imx227Device(zx_device_t* device);

    // Methods required by the ddk mixins.
    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

    // Methods for IOCTLs.
    zx_status_t GetInfo(zircon_camera_SensorInfo* out_info);

    // Methods for FIDL Message.
    zx_status_t Init();
    void DeInit();
    void SetMode(uint8_t mode);
    void StartStreaming();
    void StopStreaming();
    int32_t AllocateAnalogGain(int32_t gain);
    int32_t AllocateDigitalGain(int32_t gain);
    void Update();

private:
    zx_status_t InitPdev(zx_device_t* parent);

    void ShutDown();
};

} // namespace camera
