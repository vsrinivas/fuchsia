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

typedef struct pipe_buffer_allocation {
    uint16_t start;
    uint16_t end;
} pipe_buffer_allocation_t;

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

    void FinishInit();

    void SetDisplayControllerCb(void* cb_ctx, display_controller_cb_t* cb);
    zx_status_t GetDisplayInfo(uint64_t display_id, display_info_t* info);
    zx_status_t ImportVmoImage(image_t* image, const zx::vmo& vmo, size_t offset);
    void ReleaseImage(image_t* image);
    void CheckConfiguration(const display_config_t** display_config,
                            uint32_t** layer_cfg_result, uint32_t display_count);
    void ApplyConfiguration(const display_config_t** display_config, uint32_t display_count);
    uint32_t ComputeLinearStride(uint32_t width, zx_pixel_format_t format);
    zx_status_t AllocateVmo(uint64_t size, zx_handle_t* vmo_out);

    const fbl::unique_ptr<GttRegion>& GetGttRegion(void* handle);

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
    Interrupts* interrupts() { return &interrupts_; }
    uint16_t device_id() const { return device_id_; }
    const IgdOpRegion& igd_opregion() const { return igd_opregion_; }
    Power* power() { return &power_; }

    void HandleHotplug(registers::Ddi ddi, bool long_pulse);
    void HandlePipeVsync(registers::Pipe pipe, zx_time_t timestamp);

    void ResetPipe(registers::Pipe pipe) __TA_NO_THREAD_SAFETY_ANALYSIS;
    bool ResetTrans(registers::Trans trans);
    bool ResetDdi(registers::Ddi ddi);

    registers::Dpll SelectDpll(bool is_edp, bool is_hdmi, uint32_t rate);
private:
    void EnableBacklight(bool enable);
    void InitDisplays();
    fbl::unique_ptr<DisplayDevice> InitDisplay(registers::Ddi ddi) __TA_REQUIRES(display_lock_);
    zx_status_t AddDisplay(fbl::unique_ptr<DisplayDevice>&& display) __TA_REQUIRES(display_lock_);
    bool BringUpDisplayEngine(bool resume) __TA_REQUIRES(display_lock_);
    void InitDisplayBuffers();
    DisplayDevice* FindDevice(uint64_t display_id) __TA_REQUIRES(display_lock_);

    // Gets the layer_t* config for the given pipe/plane. Return false if there is no layer.
    bool GetLayer(registers::Pipe pipe, uint32_t plane,
                  const display_config_t** configs, uint32_t display_count,
                  const layer_t** layer_out) __TA_REQUIRES(display_lock_);
    // Returns false if no allocation is possible. When that happens,
    // plane 0 of the failing displays will be set to UINT16_MAX.
    bool CalculateMinimumAllocations(const display_config_t** display_configs,
                                     uint32_t display_count,
                                     uint16_t min_allocs[registers::kPipeCount]
                                                        [registers::kImagePlaneCount])
                                     __TA_REQUIRES(display_lock_);
    // Updates plane_buffers_ based pipe_buffers_ and the given parameters
    void UpdateAllocations(const uint16_t min_allocs[registers::kPipeCount]
                                                    [registers::kImagePlaneCount],
                           const uint64_t display_rate[registers::kPipeCount]
                                                      [registers::kImagePlaneCount])
                           __TA_REQUIRES(display_lock_);
    // Reallocates the pipe buffers when a pipe comes online/goes offline. This is a
    // long-running operation, as shifting allocations between pipes requires waiting
    // for vsync.
    void ReallocatePipeBuffers(bool is_hotplug) __TA_REQUIRES(display_lock_);
    // Reallocates plane buffers based on the given layer config.
    bool ReallocatePlaneBuffers(const display_config_t** display_configs,
                                uint32_t display_count) __TA_REQUIRES(display_lock_);

    zx_device_t* zx_gpu_dev_ = nullptr;
    bool gpu_released_ = false;
    bool display_released_ = false;

    void* dc_cb_ctx_ __TA_GUARDED(_dc_cb_lock_);
    display_controller_cb_t* _dc_cb_ __TA_GUARDED(_dc_cb_lock_) = nullptr;
    mtx_t _dc_cb_lock_;
    bool ready_for_callback_ __TA_GUARDED(_dc_cb_lock_) = false;

    // To prevent deadlocks, require that only the callback lock is held when making
    // callbacks and that the callback lock is acquired before any other locks.
    display_controller_cb_t* dc_cb() __TA_REQUIRES(_dc_cb_lock_)
                                     __TA_EXCLUDES(display_lock_, gtt_lock_, bar_lock_) {
        return _dc_cb_;
    }
    // TODO: Use __TA_ACQUIRED_BEFORE when it works as well as __TA_EXCLUDES
    void acquire_dc_cb_lock() __TA_ACQUIRE(_dc_cb_lock_)
                              __TA_EXCLUDES(display_lock_, gtt_lock_, bar_lock_) {
        mtx_lock(&_dc_cb_lock_);
    }
    void release_dc_cb_lock() __TA_RELEASE(_dc_cb_lock_) { mtx_unlock(&_dc_cb_lock_); }

    Gtt gtt_ __TA_GUARDED(gtt_lock_);
    mtx_t gtt_lock_;
    // These regions' VMOs are not owned
    fbl::Vector<fbl::unique_ptr<GttRegion>> imported_images_ __TA_GUARDED(gtt_lock_);
    // These regions' VMOs are owned
    fbl::Vector<fbl::unique_ptr<GttRegion>> imported_gtt_regions_ __TA_GUARDED(gtt_lock_);

    IgdOpRegion igd_opregion_; // Read only, no locking
    Interrupts interrupts_; // Internal locking

    pci_protocol_t pci_;
    struct {
        void* base;
        uint64_t size;
        zx_handle_t vmo;
        int32_t count = 0;
    } mapped_bars_[PCI_MAX_BAR_COUNT] __TA_GUARDED(bar_lock_);
    mtx_t bar_lock_;
    // The mmio_space_ unique_ptr is read only. The internal registers are
    // guarded by various locks where appropriate.
    fbl::unique_ptr<hwreg::RegisterIo> mmio_space_;

    // References to displays. References are owned by devmgr, but will always
    // be valid while they are in this vector.
    fbl::Vector<fbl::unique_ptr<DisplayDevice>> display_devices_ __TA_GUARDED(display_lock_);
    uint64_t next_id_ __TA_GUARDED(display_lock_) = 1; // id can't be INVALID_DISPLAY_ID == 0
    mtx_t display_lock_;

    Power power_;
    PowerWellRef cd_clk_power_well_;
    struct {
        uint8_t use_count = 0;
        bool is_hdmi;
        uint32_t rate;
    } dplls_[registers::kDpllCount] = {};

    // Buffer allocations for individual planes
    struct {
        // Start of the allocation, or registers::PlaneBufCfg::kBufferCount if no allocation
        uint16_t start;
        // End of the allocation (exclusive), or registers::PlaneBufCfg::kBufferCount if no alloc
        uint16_t end;
        // The length of the minimum allocation required by the plane. 0 if no
        // allocation is required.
        uint16_t minimum;
    } plane_buffers_[registers::kPipeCount][registers::kImagePlaneCount]
            __TA_GUARDED(display_lock_) = {};
    // Buffer allocations for pipes
    pipe_buffer_allocation_t pipe_buffers_[registers::kPipeCount] __TA_GUARDED(display_lock_) = {};

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

    bool init_thrd_started_ = false;
    thrd_t init_thread_;
};

} // namespace i915

#endif // __cplusplus

__BEGIN_CDECLS
zx_status_t intel_i915_bind(void* ctx, zx_device_t* parent);
__END_CDECLS
