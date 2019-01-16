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
#include <ddktl/protocol/isp.h>
#include <fbl/unique_ptr.h>
#include <threads.h>

namespace camera {
// |ArmIspDevice| is spawned by the driver in |arm-isp.cpp|
// This class provides the ZX_PROTOCOL_ISP ops for all of it's
// children. This is TBD as to which protocol it will provide.
// Most likely its the ZX_PROTOCOL_CAMERA once we move it from
// Garnet to Zircon.
class ArmIspDevice;
using IspDeviceType = ddk::Device<ArmIspDevice, ddk::Unbindable>;

class ArmIspDevice : public IspDeviceType,
                     public ddk::IspProtocol<ArmIspDevice, ddk::base_protocol> {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ArmIspDevice);

    explicit ArmIspDevice(zx_device_t* parent)
        : IspDeviceType(parent) {}

    ~ArmIspDevice();

    static zx_status_t Create(zx_device_t* parent);

    // Methods required by the ddk.
    void DdkRelease();
    void DdkUnbind();

    // ZX_PROTOCOL_ISP ops.
    void IspDummyCall() {};

private:
    void ShutDown();
};

} // namespace camera
