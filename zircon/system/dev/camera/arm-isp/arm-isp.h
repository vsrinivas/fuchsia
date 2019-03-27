// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "global_regs.h"
#include "pingpong_regs.h"
#include "stats-mgr.h"

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
#include <hw/reg.h>
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
                          mmio_buffer_t local_mmio,
                          zx::interrupt isp_irq,
                          zx::bti bti,
                          isp_callbacks_t sensor_callbacks)
        : IspDeviceType(parent), pdev_(parent),
          hiu_mmio_(std::move(hiu_mmio)), power_mmio_(std::move(power_mmio)),
          memory_pd_mmio_(std::move(memory_pd_mmio)), reset_mmio_(std::move(reset_mmio)),
          isp_mmio_(std::move(isp_mmio)), isp_mmio_local_(local_mmio, 0),
          isp_irq_(std::move(isp_irq)), bti_(std::move(bti)),
          sensor_callbacks_(sensor_callbacks) {}

    ~ArmIspDevice();

    static zx_status_t Create(zx_device_t* parent, isp_callbacks_t cbs);

    // Methods required by the ddk.
    void DdkRelease();
    void DdkUnbind();

    // ZX_PROTOCOL_ISP ops.
    void IspDummyCall() {}

    // ISP Init Sequences (init_sequences.cpp)
    void IspLoadSeq_linear();
    void IspLoadSeq_settings();
    void IspLoadSeq_fs_lin_2exp();
    void IspLoadSeq_fs_lin_3exp();
    void IspLoadSeq_fs_lin_4exp();
    void IspLoadSeq_settings_context();
    void IspLoadCustomSequence();

private:
    zx_status_t InitIsp();
    zx_status_t IspContextInit();
    void ShutDown();
    void PowerUpIsp();
    void IspHWReset(bool reset);
    int IspIrqHandler();
    void CopyContextInfo(uint8_t config_space,
                         uint8_t direction);
    void CopyMeteringInfo(uint8_t config_space,
                          uint8_t direction);

    ddk::PDev pdev_;

    ddk::MmioBuffer hiu_mmio_;
    ddk::MmioBuffer power_mmio_;
    ddk::MmioBuffer memory_pd_mmio_;
    ddk::MmioBuffer reset_mmio_;
    ddk::MmioBuffer isp_mmio_;
    // MmioView is currently used and created using a custom mmio_buffer_t
    // populated with malloced memory.
    // We can switch to using the actual mmio_buffer_t
    // when we plan to use SW-HW context, in order to make a easy switch.
    ddk::MmioView isp_mmio_local_;

    zx::interrupt isp_irq_;
    thrd_t irq_thread_;
    zx::bti bti_;
    std::atomic<bool> running_;

    isp_callbacks_t sensor_callbacks_;

    fbl::unique_ptr<camera::StatsManager> statsMgr_;
};

} // namespace camera
