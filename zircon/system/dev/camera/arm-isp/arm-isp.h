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
#include <ddktl/protocol/isp.h>
#include <ddktl/protocol/ispimpl.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/interrupt.h>
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

    explicit ArmIspDevice(zx_device_t* parent,
                          ddk ::MmioBuffer hiu_mmio,
                          ddk ::MmioBuffer power_mmio,
                          ddk ::MmioBuffer memory_pd_mmio,
                          ddk ::MmioBuffer reset_mmio,
                          ddk ::MmioBuffer isp_mmio,
                          zx::interrupt isp_irq)
        : IspDeviceType(parent), pdev_(parent),
          hiu_mmio_(std::move(hiu_mmio)), power_mmio_(std::move(power_mmio)),
          memory_pd_mmio_(std::move(memory_pd_mmio)), reset_mmio_(std::move(reset_mmio)),
          isp_mmio_(std::move(isp_mmio)), isp_irq_(std::move(isp_irq)) {}

    ~ArmIspDevice();

    static zx_status_t Create(zx_device_t* parent);

    // Methods required by the ddk.
    void DdkRelease();
    void DdkUnbind();

    // ZX_PROTOCOL_ISP ops.
    void IspDummyCall(){};

    void IspLoadSeq_linear();
    void IspLoadSeq_settings();
    void IspLoadSeq_fs_lin_2exp();
    void IspLoadSeq_fs_lin_3exp();
    void IspLoadSeq_fs_lin_4exp();
    void IspLoadSeq_settings_context();
    void IspLoadCustomSequence();

private:
    void ShutDown();
    void PowerUpIsp();
    void IspHWReset(bool reset);
    zx_status_t InitIsp();
    int IspIrqHandler();

    ddk::PDev pdev_;

    ddk::MmioBuffer hiu_mmio_;
    ddk::MmioBuffer power_mmio_;
    ddk::MmioBuffer memory_pd_mmio_;
    ddk::MmioBuffer reset_mmio_;
    ddk::MmioBuffer isp_mmio_;

    zx::interrupt isp_irq_;
    thrd_t irq_thread_;
    std::atomic<bool> running_;
};

} // namespace camera
