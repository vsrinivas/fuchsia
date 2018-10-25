// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>
#include <ddk/protocol/mipicsi.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddk/protocol/platform-device.h>
#include <ddktl/device.h>
#include <ddktl/mmio.h>
#include <fbl/atomic.h>
#include <fbl/unique_ptr.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/zx/interrupt.h>
#include <threads.h>

namespace camera {

class AmlMipiDevice {
public:
    static zx_status_t Create(zx_device_t* parent);
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlMipiDevice);
    AmlMipiDevice(){};
    ~AmlMipiDevice();
    zx_device_t* device_;

    // Methods required by the ddk.
    // TODO(braval) - Use DDKTL to add child devices when
    //                support is added (ZX-2793)
    void DdkRelease(void* ctx);
    void DdkUnbind(void* ctx);
    void ShutDown();

    // Methods for ZX_PROTOCOL_MIPI_CSI.
    static zx_status_t MipiCsiInit(void* ctx,
                                   const mipi_info_t* mipi_info,
                                   const mipi_adap_info_t* adap_info);
    static zx_status_t MipiCsiDeInit(void* ctx);

private:
    fbl::unique_ptr<ddk::MmioBuffer> csi_phy0_mmio_;
    fbl::unique_ptr<ddk::MmioBuffer> aphy0_mmio_;
    fbl::unique_ptr<ddk::MmioBuffer> csi_host0_mmio_;
    fbl::unique_ptr<ddk::MmioBuffer> mipi_adap_mmio_;
    fbl::unique_ptr<ddk::MmioBuffer> hiu_mmio_;
    fbl::unique_ptr<ddk::MmioBuffer> power_mmio_;
    fbl::unique_ptr<ddk::MmioBuffer> memory_pd_mmio_;
    fbl::unique_ptr<ddk::MmioBuffer> reset_mmio_;

    pdev_protocol_t pdev_;

    zx::bti bti_;
    zx::interrupt adap_irq_;
    fbl::atomic<bool> running_;
    thrd_t irq_thread_;

    zx::vmo ring_buffer_vmo_;
    fzl::PinnedVmo pinned_ring_buffer_;

    zx_status_t InitBuffer(const mipi_adap_info_t* info, size_t size);
    zx_status_t InitPdev(zx_device_t* parent);
    uint32_t AdapGetDepth(const mipi_adap_info_t* info);
    int AdapterIrqHandler();

    // MIPI Iternal APIs
    void MipiPhyInit(const mipi_info_t* info);
    void MipiCsi2Init(const mipi_info_t* info);
    void MipiPhyReset();
    void MipiCsi2Reset();

    // Mipi Adapter APIs
    zx_status_t MipiAdapInit(const mipi_adap_info_t* info);
    void MipiAdapStart(const mipi_adap_info_t* info);
    void MipiAdapReset();
    void MipiAdapStart();

    // Initialize the MIPI host to 720p by default.
    zx_status_t AdapFrontendInit(const mipi_adap_info_t* info);
    zx_status_t AdapReaderInit(const mipi_adap_info_t* info);
    zx_status_t AdapPixelInit(const mipi_adap_info_t* info);
    zx_status_t AdapAlignInit(const mipi_adap_info_t* info);

    // Set it up for the correct format and resolution.
    void AdapAlignStart(const mipi_adap_info_t* info);
    void AdapPixelStart(const mipi_adap_info_t* info);
    void AdapReaderStart(const mipi_adap_info_t* info);
    void AdapFrontEndStart(const mipi_adap_info_t* info);

    // MIPI & ISP Power and clock APIs.
    void InitMipiClock();
    void PowerUpIsp();
    void IspHWReset(bool reset);

    // Debug.
    void DumpCsiPhyRegs();
    void DumpAPhyRegs();
    void DumpHostRegs();
    void DumpFrontEndRegs();
    void DumpReaderRegs();
    void DumpAlignRegs();
    void DumpPixelRegs();
    void DumpMiscRegs();
};

} // namespace camera
