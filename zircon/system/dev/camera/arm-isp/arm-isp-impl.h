// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <ddk/metadata/camera.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/pdev.h>
#include <ddktl/protocol/ispimpl.h>
#include <fbl/unique_ptr.h>
#include <threads.h>

namespace camera {

// |ArmISPImplDevice| is spawned by the driver in |arm-isp-impl.cpp|
// to which the MIPI CSI driver binds to.
// This class provides the ZX_PROTOCOL_ISP_IMPL ops for all of it's
// children.
class ArmISPImplDevice;
using DeviceType = ddk::Device<ArmISPImplDevice, ddk::Unbindable>;

class ArmISPImplDevice : public DeviceType,
                         public ddk::IspImplProtocol<ArmISPImplDevice, ddk::base_protocol> {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ArmISPImplDevice);

    explicit ArmISPImplDevice(zx_device_t* parent)
        : DeviceType(parent), pdev_(parent) {}

    static zx_status_t Create(zx_device_t* parent);

    ~ArmISPImplDevice();

    // Methods required by the ddk.
    void DdkRelease();
    void DdkUnbind();

    // ZX_PROTOCOL_ISP_IMPL ops.
    zx_status_t IspImplRegisterCallbacks(const isp_callbacks_protocol_t* cb);
    zx_status_t IspImplDeRegisterCallbacks();

private:
    zx_status_t InitPdev(zx_device_t* parent);
    zx_status_t Bind(mipi_adapter_t* mipi_info);
    void ShutDown();
    int WorkerThread();

    ddk::PDev pdev_;

    thrd_t worker_thread_;

    isp_callbacks_protocol_t sensor_callbacks_;

    sync_completion_t cb_registered_signal_;

    zx_device_t* parent_;
};

} // namespace camera
