// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/pdev.h>
#include <ddktl/protocol/gdc.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <lib/fidl-utils/bind.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/interrupt.h>
#include <threads.h>
#include <zircon/fidl.h>

namespace camera {
// |GdcDevice| is spawned by the driver in |gdc.cpp|
// This provides ZX_PROTOCOL_GDC.
class GdcDevice;
using GdcDeviceType = ddk::Device<GdcDevice, ddk::Unbindable>;

class GdcDevice : public GdcDeviceType,
                  public ddk::GdcProtocol<GdcDevice, ddk::base_protocol> {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(GdcDevice);

    explicit GdcDevice(zx_device_t* parent,
                       ddk ::MmioBuffer clk_mmio,
                       ddk ::MmioBuffer memory_pd_mmio,
                       ddk ::MmioBuffer gdc_mmio,
                       zx::interrupt gdc_irq,
                       zx::bti bti)
        : GdcDeviceType(parent), clock_mmio_(std::move(clk_mmio)),
          memory_pd_mmio_(std::move(memory_pd_mmio)), gdc_mmio_(std::move(gdc_mmio)),
          gdc_irq_(std::move(gdc_irq)), bti_(std::move(bti)) {}

    ~GdcDevice();

    // Setup() is used to create an instance of GdcDevice.
    // It sets up the pdev & brings the GDC out of reset.
    static zx_status_t Setup(void* ctx, zx_device_t* parent, std::unique_ptr<GdcDevice>* out);

    // Methods required by the ddk.
    void DdkRelease();
    void DdkUnbind();

    // ZX_PROTOCOL_GDC (Refer to gdc.banjo for documentation).
    zx_status_t GdcInitTask(const buffer_collection_info_t* input_buffer_collection,
                            const buffer_collection_info_t* output_buffer_collection,
                            zx::vmo config_vmo,
                            const gdc_callback_t* callback,
                            uint32_t* out_task_index);
    zx_status_t GdcProcessFrame(uint32_t task_index, uint32_t input_buffer_index);
    void GdcRemoveTask(uint32_t task_index);
    void GdcReleaseFrame(uint32_t task_index, uint32_t buffer_index);

private:
    // All necessary clean up is done here in ShutDown().
    void ShutDown();
    void InitClocks();

    ddk::MmioBuffer clock_mmio_;
    ddk::MmioBuffer memory_pd_mmio_;
    ddk::MmioBuffer gdc_mmio_;
    zx::interrupt gdc_irq_;
    zx::bti bti_;
    thrd_t irq_thread_;
    std::atomic<bool> running_;
};

} // namespace camera
