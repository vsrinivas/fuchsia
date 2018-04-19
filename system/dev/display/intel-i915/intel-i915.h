// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#if __cplusplus

#include <ddk/protocol/intel-gpu-core.h>
#include <ddk/protocol/pci.h>
#include <ddktl/protocol/display-controller.h>

#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <hwreg/mmio.h>
#include <threads.h>

#include "display-device.h"
#include "gtt.h"
#include "igd.h"
#include "interrupts.h"
#include "power.h"
#include "registers.h"
#include "registers-ddi.h"
#include "registers-dpll.h"
#include "registers-pipe.h"
#include "registers-transcoder.h"

namespace i915 {

class Controller;
using DeviceType = ddk::Device<Controller, ddk::Unbindable, ddk::Suspendable, ddk::Resumable>;

class Controller : public DeviceType, public ddk::DisplayControllerProtocol<Controller> {
public:
    Controller(zx_device_t* parent);
    ~Controller();

    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkSuspend(uint32_t reason);
    zx_status_t DdkResume(uint32_t reason);
    zx_status_t Bind(fbl::unique_ptr<i915::Controller>* controller_ptr);

    void SetDisplayControllerCb(void* cb_ctx, display_controller_cb_t* cb);
    zx_status_t GetDisplayInfo(int32_t display_id, display_info_t* info);
    zx_status_t ImportVmoImage(image_t* image, const zx::vmo& vmo, size_t offset);
    void ReleaseImage(image_t* image);
    bool CheckConfiguration(display_config_t** display_config, uint32_t display_count);
    void ApplyConfiguration(display_config_t** display_config, uint32_t display_count);
    uint32_t ComputeLinearStride(uint32_t width, zx_pixel_format_t format);
    zx_status_t AllocateVmo(uint64_t size, zx_handle_t* vmo_out);

    zx_status_t ReadPciConfig16(uint16_t addr, uint16_t* value_out);
    zx_status_t MapPciMmio(uint32_t pci_bar, void** addr_out, uint64_t* size_out);
    zx_status_t UnmapPciMmio(uint32_t pci_bar);
    zx_status_t GetPciBti(uint32_t index, zx_handle_t* bti_out);
    zx_status_t RegisterInterruptCallback(zx_intel_gpu_core_interrupt_callback_t callback,
                                          void* data, uint32_t interrupt_mask);
    zx_status_t UnregisterInterruptCallback();
    uint64_t GttGetSize();
    zx_status_t GttAlloc(uint64_t page_count, uint64_t* addr_out);
    zx_status_t GttFree(uint64_t addr);
    zx_status_t GttClear(uint64_t addr);
    zx_status_t GttInsert(uint64_t addr, zx_handle_t buffer,
                          uint64_t page_offset, uint64_t page_count);
    void GpuRelease();

    pci_protocol_t* pci() { return &pci_; }
    hwreg::RegisterIo* mmio_space() { return mmio_space_.get(); }
    Gtt* gtt() { return &gtt_; }
    uint16_t device_id() const { return device_id_; }
    const IgdOpRegion& igd_opregion() const { return igd_opregion_; }
    Power* power() { return &power_; }

    void HandleHotplug(registers::Ddi ddi, bool long_pulse);
    void HandlePipeVsync(registers::Pipe pipe);

    void ResetPipe(registers::Pipe pipe);
    bool ResetTrans(registers::Trans trans);
    bool ResetDdi(registers::Ddi ddi);

    registers::Dpll SelectDpll(bool is_edp, bool is_hdmi, uint32_t rate);
private:
    void EnableBacklight(bool enable);
    zx_status_t InitDisplays();
    fbl::unique_ptr<DisplayDevice> InitDisplay(registers::Ddi ddi);
    zx_status_t AddDisplay(fbl::unique_ptr<DisplayDevice>&& display);
    bool BringUpDisplayEngine(bool resume);
    void AllocDisplayBuffers();
    DisplayDevice* FindDevice(int32_t display_id);

    zx_device_t* zx_gpu_dev_;
    bool gpu_released_ = false;
    bool display_released_ = false;

    void* dc_cb_ctx_;
    display_controller_cb_t* dc_cb_;

    Gtt gtt_;
    IgdOpRegion igd_opregion_;
    Interrupts interrupts_;

    pci_protocol_t pci_;
    struct {
        void* base;
        uint64_t size;
        zx_handle_t vmo;
        int32_t count;
    } mapped_bars_[PCI_MAX_BAR_COUNT];
    fbl::unique_ptr<hwreg::RegisterIo> mmio_space_;

    // These regions' VMOs are not owned
    fbl::Vector<fbl::unique_ptr<GttRegion>> imported_images_;
    // These regions' VMOs are owned
    fbl::Vector<fbl::unique_ptr<GttRegion>> imported_gtt_regions_;

    // References to displays. References are owned by devmgr, but will always
    // be valid while they are in this vector.
    fbl::Vector<DisplayDevice*> display_devices_;
    int32_t next_id_ = 0;

    Power power_;
    PowerWellRef cd_clk_power_well_;
    struct {
        uint8_t use_count;
        bool is_hdmi;
        uint32_t rate;
    } dplls_[registers::kDpllCount] = {};

    uint16_t device_id_;
    uint32_t flags_;

    // Various configuration values set by the BIOS which need to be carried across suspend.
    uint32_t pp_divisor_val_;
    uint32_t pp_off_delay_val_;
    uint32_t pp_on_delay_val_;
    uint32_t sblc_ctrl2_val_;
    uint32_t schicken1_val_;
    bool ddi_a_lane_capability_control_;
    bool sblc_polarity_;
};

} // namespace i915

#endif // __cplusplus

__BEGIN_CDECLS
zx_status_t intel_i915_bind(void* ctx, zx_device_t* parent);
__END_CDECLS
