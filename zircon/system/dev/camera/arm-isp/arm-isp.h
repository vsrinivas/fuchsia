// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arm-isp-test.h"
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
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/isp.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/hardware/camera/c/fidl.h>
#include <hw/reg.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fit/function.h>
#include <lib/zx/interrupt.h>
#include <threads.h>
#include <zircon/fidl.h>

namespace camera {
// |ArmIspDevice| is spawned by the driver in |arm-isp.cpp|
// This provides the interface provided in camera.fidl in Zircon.
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
                          zx_device_t* camera_sensor)
        : IspDeviceType(parent), pdev_(parent),
          hiu_mmio_(std::move(hiu_mmio)), power_mmio_(std::move(power_mmio)),
          memory_pd_mmio_(std::move(memory_pd_mmio)), reset_mmio_(std::move(reset_mmio)),
          isp_mmio_(std::move(isp_mmio)), isp_mmio_local_(local_mmio, 0),
          isp_irq_(std::move(isp_irq)), bti_(std::move(bti)),
          camera_sensor_(camera_sensor) {}

    ~ArmIspDevice();

    static zx_status_t Create(void* ctx, zx_device_t* parent);

    // Methods required by the ddk.
    void DdkRelease();
    void DdkUnbind();

    // ZX_PROTOCOL_ISP
    zx_status_t IspCreateInputStream(const buffer_collection_info_t* buffer_collection,
                                     const frame_rate_t* rate,
                                     const input_stream_callback_t* stream,
                                     input_stream_protocol_t* out_s);

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

    // A skeleton function for testing the ISP with the ISPDeviceTester:
    zx_status_t RunTests() { return ZX_OK; }

    void ShutDown();
    void PowerUpIsp();
    void IspHWReset(bool reset);
    int IspIrqHandler();
    void CopyContextInfo(uint8_t config_space,
                         uint8_t direction);
    void CopyMeteringInfo(uint8_t config_space);
    zx_status_t SetPort(uint8_t kMode);
    bool IsFrameProcessingInProgress();

    zx_status_t StartStreaming();
    zx_status_t StopStreaming();

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

    ddk::CameraSensorProtocolClient camera_sensor_;

    fbl::unique_ptr<camera::StatsManager> statsMgr_;

    sync_completion_t frame_processing_signal_;

    // Callback to call when calling DdkUnbind,
    // so the ArmIspDeviceTester (if it exists) stops interfacing
    // with this class.
    fit::callback<void()> on_isp_unbind_;
    // This lock prevents this class from being unbound while it's child is being set up:
    fbl::Mutex unbind_lock_;
    friend class ArmIspDeviceTester;
};

} // namespace camera
