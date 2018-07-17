// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/intel-gpu-core.h>
#include <ddk/protocol/pci.h>
#include <hw/inout.h>
#include <hw/pci.h>

#include <assert.h>
#include <fbl/unique_ptr.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include "dp-display.h"
#include "hdmi-display.h"
#include "intel-i915.h"
#include "macros.h"
#include "pci-ids.h"
#include "registers.h"
#include "registers-ddi.h"
#include "registers-dpll.h"
#include "registers-pipe.h"
#include "registers-transcoder.h"
#include "registers.h"
#include "tiling.h"

#define INTEL_I915_BROADWELL_DID (0x1616)

#define INTEL_I915_REG_WINDOW_SIZE (0x1000000u)
#define INTEL_I915_FB_WINDOW_SIZE (0x10000000u)

#define BACKLIGHT_CTRL_OFFSET (0xc8250)
#define BACKLIGHT_CTRL_BIT ((uint32_t)(1u << 31))

#define FLAGS_BACKLIGHT 1

namespace {
static const zx_pixel_format_t supported_formats[2] = {
    ZX_PIXEL_FORMAT_ARGB_8888, ZX_PIXEL_FORMAT_RGB_x888
};

static const cursor_info_t cursor_infos[3] = {
    { .width = 64, .height = 64, .format = ZX_PIXEL_FORMAT_ARGB_8888 },
    { .width = 128, .height = 128, .format = ZX_PIXEL_FORMAT_ARGB_8888 },
    { .width = 256, .height = 256, .format = ZX_PIXEL_FORMAT_ARGB_8888 },
};

bool pipe_in_use(const fbl::Vector<fbl::unique_ptr<i915::DisplayDevice>>& displays,
                 registers::Pipe pipe) {
    for (size_t i = 0; i < displays.size(); i++) {
        if (displays[i]->pipe() == pipe) {
            return true;
        }
    }
    return false;
}

static zx_status_t read_pci_config_16(void* ctx, uint16_t addr, uint16_t* value_out) {
    return static_cast<i915::Controller*>(ctx)->ReadPciConfig16(addr, value_out);
}

static zx_status_t map_pci_mmio(void* ctx, uint32_t pci_bar, void** addr_out, uint64_t* size_out) {
    return static_cast<i915::Controller*>(ctx)->MapPciMmio(pci_bar, addr_out, size_out);
}

static zx_status_t unmap_pci_mmio(void* ctx, uint32_t pci_bar) {
    return static_cast<i915::Controller*>(ctx)->UnmapPciMmio(pci_bar);
}

static zx_status_t get_pci_bti(void* ctx, uint32_t index, zx_handle_t* bti_out) {
    return static_cast<i915::Controller*>(ctx)->GetPciBti(index, bti_out);
}

static zx_status_t register_interrupt_callback(void* ctx,
                                               zx_intel_gpu_core_interrupt_callback_t callback,
                                               void* data, uint32_t interrupt_mask) {
    return static_cast<i915::Controller*>(ctx)
            ->RegisterInterruptCallback(callback, data, interrupt_mask);
}

static zx_status_t unregister_interrupt_callback(void* ctx) {
    return static_cast<i915::Controller*>(ctx)->UnregisterInterruptCallback();
}

static uint64_t gtt_get_size(void* ctx) {
    return static_cast<i915::Controller*>(ctx)->GttGetSize();
}

static zx_status_t gtt_alloc(void* ctx, uint64_t page_count, uint64_t* addr_out) {
    return static_cast<i915::Controller*>(ctx)->GttAlloc(page_count, addr_out);
}

static zx_status_t gtt_free(void* ctx, uint64_t addr) {
    return static_cast<i915::Controller*>(ctx)->GttFree(addr);
}

static zx_status_t gtt_clear(void* ctx, uint64_t addr) {
    return static_cast<i915::Controller*>(ctx)->GttClear(addr);
}

static zx_status_t gtt_insert(void* ctx, uint64_t addr, zx_handle_t buffer, uint64_t page_offset,
                              uint64_t page_count) {
    return static_cast<i915::Controller*>(ctx)->GttInsert(addr, buffer, page_offset, page_count);
}

static zx_intel_gpu_core_protocol_ops_t i915_gpu_core_protocol_ops = {
    .read_pci_config_16 = read_pci_config_16,
    .map_pci_mmio = map_pci_mmio,
    .unmap_pci_mmio = unmap_pci_mmio,
    .get_pci_bti = get_pci_bti,
    .register_interrupt_callback = register_interrupt_callback,
    .unregister_interrupt_callback = unregister_interrupt_callback,
    .gtt_get_size = gtt_get_size,
    .gtt_alloc = gtt_alloc,
    .gtt_free = gtt_free,
    .gtt_clear = gtt_clear,
    .gtt_insert = gtt_insert
};

static void gpu_release(void* ctx) {
    static_cast<i915::Controller*>(ctx)->GpuRelease();
}

static zx_protocol_device_t i915_gpu_core_device_proto = {};

static int finish_init(void* arg) {
    static_cast<i915::Controller*>(arg)->FinishInit();
    return 0;
}

} // namespace

namespace i915 {

uint32_t Controller::DisplayModeToRefreshRate(const display_mode_t* mode) {
    double total_pxls = (mode->h_addressable + mode->h_blanking) *
            (mode->v_addressable + mode->v_blanking);
    double pixel_clock_hz = mode->pixel_clock_10khz * 1000 * 10;
    return static_cast<uint32_t>(round(pixel_clock_hz / total_pxls));
}

void Controller::EnableBacklight(bool enable) {
    if (flags_ & FLAGS_BACKLIGHT) {
        uint32_t tmp = mmio_space_->Read<uint32_t>(BACKLIGHT_CTRL_OFFSET);

        if (enable) {
            tmp |= BACKLIGHT_CTRL_BIT;
        } else {
            tmp &= ~BACKLIGHT_CTRL_BIT;
        }

        mmio_space_->Write<uint32_t>(BACKLIGHT_CTRL_OFFSET, tmp);
    }
}

void Controller::HandleHotplug(registers::Ddi ddi, bool long_pulse) {
    LOG_TRACE("Hotplug detected on ddi %d (long_pulse=%d)\n", ddi, long_pulse);
    fbl::unique_ptr<DisplayDevice> device = nullptr;
    uint64_t display_added = INVALID_DISPLAY_ID;
    uint64_t display_removed = INVALID_DISPLAY_ID;

    acquire_dc_cb_lock();
    {
        fbl::AutoLock lock(&display_lock_);

        for (size_t i = 0; i < display_devices_.size(); i++) {
            if (display_devices_[i]->ddi() == ddi) {
                if (display_devices_[i]->HandleHotplug(long_pulse)) {
                    LOG_SPEW("hotplug handled by device\n");
                    release_dc_cb_lock();
                    return;
                }
                device = display_devices_.erase(i);
                break;
            }
        }
        if (device) { // Existing device was unplugged
            LOG_INFO("Display %ld unplugged\n", device->id());
            display_removed = device->id();
            // Make sure the display's resources get freed before reallocating the pipe buffers
            device.reset();
        } else { // New device was plugged in
            fbl::unique_ptr<DisplayDevice> device = InitDisplay(ddi);
            if (!device) {
                LOG_INFO("failed to init hotplug display\n");
            } else {
                uint64_t id = device->id();
                if (AddDisplay(fbl::move(device)) == ZX_OK) {
                    display_added = id;
                }
            }
        }

        ReallocatePipeBuffers(true);
    }
    if (dc_cb() && (display_added != INVALID_DISPLAY_ID || display_removed != INVALID_DISPLAY_ID)) {
        dc_cb()->on_displays_changed(dc_cb_ctx_,
                                     &display_added, display_added != INVALID_DISPLAY_ID,
                                     &display_removed, display_removed != INVALID_DISPLAY_ID);
    }
    release_dc_cb_lock();
}

void Controller::HandlePipeVsync(registers::Pipe pipe, zx_time_t timestamp) {
    acquire_dc_cb_lock();

    if (!dc_cb()) {
        release_dc_cb_lock();
        return;
    }

    uint64_t id = INVALID_DISPLAY_ID;
    void* handles[3];
    int32_t handle_count = 0;
    {
        fbl::AutoLock lock(&display_lock_);
        for (auto& display : display_devices_) {
            if (display->pipe() == pipe) {
                id = display->id();

                registers::PipeRegs regs(pipe);
                for (int i = 0; i < 3; i++) {
                    auto live_surface = regs.PlaneSurfaceLive(i).ReadFrom(mmio_space());
                    void* handle = reinterpret_cast<void*>(
                            live_surface.surface_base_addr() << live_surface.kPageShift);
                    if (handle) {
                        handles[handle_count++] = handle;
                    }
                }

                auto live_surface = regs.CursorSurfaceLive().ReadFrom(mmio_space());
                void* handle = reinterpret_cast<void*>(
                        live_surface.surface_base_addr() << live_surface.kPageShift);
                if (handle) {
                    handles[handle_count++] = handle;
                }

                break;
            }
        }
    }

    if (id != INVALID_DISPLAY_ID && handle_count) {
        dc_cb()->on_display_vsync(dc_cb_ctx_, id, timestamp, handles, handle_count);
    }
    release_dc_cb_lock();
}

DisplayDevice* Controller::FindDevice(uint64_t display_id) {
    for (auto& d : display_devices_) {
        if (d->id() == display_id) {
            return d.get();
        }
    }
    return nullptr;
}

bool Controller::BringUpDisplayEngine(bool resume) {
    // Enable PCH Reset Handshake
    auto nde_rstwrn_opt = registers::NorthDERestetWarning::Get().ReadFrom(mmio_space_.get());
    nde_rstwrn_opt.set_rst_pch_handshake_enable(1);
    nde_rstwrn_opt.WriteTo(mmio_space_.get());

    // Wait for Power Well 0 distribution
    if (!WAIT_ON_US(registers::FuseStatus::Get().ReadFrom(mmio_space_.get()).pg0_dist_status(), 5)) {
        LOG_ERROR("Power Well 0 distribution failed\n");
        return false;
    }

    if (resume) {
        power_.Resume();
    } else {
        cd_clk_power_well_ = power_.GetCdClockPowerWellRef();
    }

    // Enable CDCLK PLL to 337.5mhz if the BIOS didn't already enable it. If it needs to be
    // something special (i.e. for eDP), assume that the BIOS already enabled it.
    auto dpll_enable = registers::DpllEnable::Get(registers::DPLL_0).ReadFrom(mmio_space_.get());
    if (!dpll_enable.enable_dpll()) {
        // Set the cd_clk frequency to the minimum
        auto cd_clk = registers::CdClockCtl::Get().ReadFrom(mmio_space_.get());
        cd_clk.set_cd_freq_select(cd_clk.kFreqSelect3XX);
        cd_clk.set_cd_freq_decimal(cd_clk.kFreqDecimal3375);
        cd_clk.WriteTo(mmio_space_.get());

        // Configure DPLL0
        auto dpll_ctl1 = registers::DpllControl1::Get().ReadFrom(mmio_space_.get());
        dpll_ctl1.dpll_link_rate(registers::DPLL_0).set(dpll_ctl1.kLinkRate810Mhz);
        dpll_ctl1.dpll_override(registers::DPLL_0).set(1);
        dpll_ctl1.dpll_hdmi_mode(registers::DPLL_0).set(0);
        dpll_ctl1.dpll_ssc_enable(registers::DPLL_0).set(0);
        dpll_ctl1.WriteTo(mmio_space_.get());

        // Enable DPLL0 and wait for it
        dpll_enable.set_enable_dpll(1);
        dpll_enable.WriteTo(mmio_space_.get());
        if (!WAIT_ON_MS(registers::Lcpll1Control::Get().ReadFrom(mmio_space_.get()).pll_lock(), 5)) {
            LOG_ERROR("Failed to configure dpll0\n");
            return false;
        }

        // Do the magic sequence for Changing CD Clock Frequency specified on
        // intel-gfx-prm-osrc-skl-vol12-display.pdf p.135
        constexpr uint32_t kGtDriverMailboxInterface = 0x138124;
        constexpr uint32_t kGtDriverMailboxData0 = 0x138128;
        constexpr uint32_t kGtDriverMailboxData1 = 0x13812c;
        mmio_space_.get()->Write<uint32_t>(kGtDriverMailboxData0, 0x3);
        mmio_space_.get()->Write<uint32_t>(kGtDriverMailboxData1, 0x0);
        mmio_space_.get()->Write<uint32_t>(kGtDriverMailboxInterface, 0x80000007);

        int count = 0;
        for (;;) {
            if (!WAIT_ON_US(mmio_space_.get()
                                    ->Read<uint32_t>(kGtDriverMailboxInterface) &
                                0x80000000,
                            150)) {
                LOG_ERROR("GT Driver Mailbox driver busy\n");
                return false;
            }
            if (mmio_space_.get()->Read<uint32_t>(kGtDriverMailboxData0) & 0x1) {
                break;
            }
            if (count++ == 3) {
                LOG_ERROR("Failed to set cd_clk\n");
                return false;
            }
            zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
        }

        cd_clk.WriteTo(mmio_space_.get());

        mmio_space_.get()->Write<uint32_t>(kGtDriverMailboxData0, 0x3);
        mmio_space_.get()->Write<uint32_t>(kGtDriverMailboxData1, 0x0);
        mmio_space_.get()->Write<uint32_t>(kGtDriverMailboxInterface, 0x80000007);
    }

    // Enable and wait for DBUF
    auto dbuf_ctl = registers::DbufCtl::Get().ReadFrom(mmio_space_.get());
    dbuf_ctl.set_power_request(1);
    dbuf_ctl.WriteTo(mmio_space_.get());

    if (!WAIT_ON_US(registers::DbufCtl::Get().ReadFrom(mmio_space_.get()).power_state(), 10)) {
        LOG_ERROR("Failed to enable DBUF\n");
        return false;
    }

    // We never use VGA, so just disable it at startup
    constexpr uint16_t kSequencerIdx = 0x3c4;
    constexpr uint16_t kSequencerData = 0x3c5;
    constexpr uint8_t kClockingModeIdx = 1;
    constexpr uint8_t kClockingModeScreenOff = (1 << 5);
    zx_status_t status = zx_ioports_request(get_root_resource(), kSequencerIdx, 2);
    if (status != ZX_OK) {
        LOG_ERROR("Failed to map vga ports\n");
        return false;
    }
    outp(kSequencerIdx, kClockingModeIdx);
    uint8_t clocking_mode = inp(kSequencerData);
    if (!(clocking_mode & kClockingModeScreenOff)) {
        outp(kSequencerIdx, inp(kSequencerData) | kClockingModeScreenOff);
        zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));

        auto vga_ctl = registers::VgaCtl::Get().ReadFrom(mmio_space());
        vga_ctl.set_vga_display_disable(1);
        vga_ctl.WriteTo(mmio_space());
    }

    for (unsigned i = 0; i < registers::kPipeCount; i++) {
        ResetPipe(registers::kPipes[i]);

        registers::PipeRegs pipe_regs(registers::kPipes[i]);

        // Disable the scalers (double buffered on PipeScalerWinSize), since
        // we don't know what state they are in at boot.
        pipe_regs.PipeScalerCtrl(0).ReadFrom(mmio_space()).set_enable(0).WriteTo(mmio_space());
        pipe_regs.PipeScalerWinSize(0).ReadFrom(mmio_space()).WriteTo(mmio_space());
        if (i != registers::PIPE_C) {
            pipe_regs.PipeScalerCtrl(1).ReadFrom(mmio_space()).set_enable(0).WriteTo(mmio_space());
            pipe_regs.PipeScalerWinSize(1).ReadFrom(mmio_space()).WriteTo(mmio_space());
        }

        // Disable the cursor watermark
        for (int wm_num = 0; wm_num < 8; wm_num++) {
            auto wm = pipe_regs.PlaneWatermark(0, wm_num).FromValue(0);
            wm.WriteTo(mmio_space());
        }

        // Disable the primary plane watermarks and reset their buffer allocation
        for (unsigned plane_num = 0; plane_num < registers::kImagePlaneCount; plane_num++) {
            for (int wm_num = 0; wm_num < 8; wm_num++) {
                auto wm = pipe_regs.PlaneWatermark(plane_num + 1, wm_num).FromValue(0);
                wm.WriteTo(mmio_space());
            }
        }
    }

    for (unsigned i = 0; i < registers::kTransCount; i++) {
        ResetTrans(registers::kTrans[i]);
    }

    for (unsigned i = 0; i < registers::kDdiCount; i++) {
        ResetDdi(registers::kDdis[i]);
    }

    for (unsigned i = 0; i < registers::kDpllCount; i++) {
        dplls_[i].use_count = 0;
    }

    return true;
}

void Controller::ResetPipe(registers::Pipe pipe) {
    registers::PipeRegs pipe_regs(pipe);

    // Disable planes, bottom color, and cursor
    for (int i = 0; i < 3; i++ ) {
        pipe_regs.PlaneControl(i).FromValue(0).WriteTo(mmio_space());
        pipe_regs.PlaneSurface(i).FromValue(0).WriteTo(mmio_space());
    }
    auto cursor_ctrl = pipe_regs.CursorCtrl().ReadFrom(mmio_space());
    cursor_ctrl.set_mode_select(cursor_ctrl.kDisabled);
    cursor_ctrl.WriteTo(mmio_space());
    pipe_regs.CursorBase().FromValue(0).WriteTo(mmio_space());
    pipe_regs.PipeBottomColor().FromValue(0).WriteTo(mmio_space());

    ZX_DEBUG_ASSERT(mtx_trylock(&display_lock_) == thrd_busy);
    for (unsigned plane_num = 0; plane_num < registers::kImagePlaneCount; plane_num++) {
        plane_buffers_[pipe][plane_num].start = registers::PlaneBufCfg::kBufferCount;
        plane_buffers_[pipe][plane_num].minimum = 0;
    }
}

bool Controller::ResetTrans(registers::Trans trans) {
    registers::TranscoderRegs trans_regs(trans);

    // Disable transcoder and wait it to stop
    auto trans_conf = trans_regs.Conf().ReadFrom(mmio_space());
    trans_conf.set_transcoder_enable(0);
    trans_conf.WriteTo(mmio_space());
    if (!WAIT_ON_MS(!trans_regs.Conf().ReadFrom(mmio_space()).transcoder_state(), 60)) {
        LOG_ERROR("Failed to reset transcoder\n");
        return false;
    }

    // Disable transcoder ddi select and clock select
    auto trans_ddi_ctl = trans_regs.DdiFuncControl().ReadFrom(mmio_space());
    trans_ddi_ctl.set_trans_ddi_function_enable(0);
    trans_ddi_ctl.set_ddi_select(0);
    trans_ddi_ctl.WriteTo(mmio_space());

    if (trans != registers::TRANS_EDP) {
        auto trans_clk_sel = trans_regs.ClockSelect().ReadFrom(mmio_space());
        trans_clk_sel.set_trans_clock_select(0);
        trans_clk_sel.WriteTo(mmio_space());
    }

    return true;
}

bool Controller::ResetDdi(registers::Ddi ddi) {
    registers::DdiRegs ddi_regs(ddi);

    // Disable the port
    auto ddi_buf_ctl = ddi_regs.DdiBufControl().ReadFrom(mmio_space());
    bool was_enabled = ddi_buf_ctl.ddi_buffer_enable();
    ddi_buf_ctl.set_ddi_buffer_enable(0);
    ddi_buf_ctl.WriteTo(mmio_space());

    auto ddi_dp_tp_ctl = ddi_regs.DdiDpTransportControl().ReadFrom(mmio_space());
    ddi_dp_tp_ctl.set_transport_enable(0);
    ddi_dp_tp_ctl.set_dp_link_training_pattern(ddi_dp_tp_ctl.kTrainingPattern1);
    ddi_dp_tp_ctl.WriteTo(mmio_space());

    if (was_enabled && !WAIT_ON_MS(ddi_regs.DdiBufControl().ReadFrom(mmio_space()).ddi_idle_status(), 8)) {
        LOG_ERROR("Port failed to go idle\n");
        return false;
    }

    // Disable IO power
    auto pwc2 = registers::PowerWellControl2::Get().ReadFrom(mmio_space());
    pwc2.ddi_io_power_request(ddi).set(0);
    pwc2.WriteTo(mmio_space());

    // Remove the PLL mapping and disable the PLL (we don't share PLLs)
    auto dpll_ctrl2 = registers::DpllControl2::Get().ReadFrom(mmio_space());
    if (!dpll_ctrl2.ddi_clock_off(ddi).get()) {
        dpll_ctrl2.ddi_clock_off(ddi).set(1);
        dpll_ctrl2.WriteTo(mmio_space());

        registers::Dpll dpll = static_cast<registers::Dpll>(dpll_ctrl2.ddi_clock_select(ddi).get());
        // Don't underflow if we're resetting at initialization
        dplls_[dpll].use_count =
                static_cast<uint8_t>(dplls_[dpll].use_count > 0 ? dplls_[dpll].use_count - 1 : 0);
        // We don't want to disable DPLL0, since that drives cdclk.
        if (dplls_[dpll].use_count == 0 && dpll != registers::DPLL_0) {
            auto dpll_enable = registers::DpllEnable::Get(dpll).ReadFrom(mmio_space());
            dpll_enable.set_enable_dpll(0);
            dpll_enable.WriteTo(mmio_space());
        }
    }

    return true;
}

registers::Dpll Controller::SelectDpll(bool is_edp, bool is_hdmi, uint32_t rate) {
    registers::Dpll res = registers::DPLL_INVALID;
    if (is_edp) {
        if (dplls_[0].use_count == 0 || dplls_[0].rate == rate) {
            res = registers::DPLL_0;
        }
    } else {
        for (unsigned i = registers::kDpllCount - 1; i > 0; i--) {
            if (dplls_[i].use_count == 0) {
                res = static_cast<registers::Dpll>(i);
            } else if (dplls_[i].is_hdmi == is_hdmi && dplls_[i].rate == rate) {
                res = static_cast<registers::Dpll>(i);
                break;
            }
        }
    }

    if (res != registers::DPLL_INVALID) {
        dplls_[res].is_hdmi = is_hdmi;
        dplls_[res].rate = rate;
        dplls_[res].use_count++;
        LOG_SPEW("Selected DPLL %d\n", res);
    } else {
        LOG_WARN("Failed to allocate DPLL\n");
    }

    return res;
}

fbl::unique_ptr<DisplayDevice> Controller::InitDisplay(registers::Ddi ddi) {
    registers::Pipe pipe;
    if (!pipe_in_use(display_devices_, registers::PIPE_A)) {
        pipe = registers::PIPE_A;
    } else if (!pipe_in_use(display_devices_, registers::PIPE_B)) {
        pipe = registers::PIPE_B;
    } else if (!pipe_in_use(display_devices_, registers::PIPE_C)) {
        pipe = registers::PIPE_C;
    } else {
        LOG_WARN("Could not allocate pipe for ddi %d\n", ddi);
        return nullptr;
    }

    fbl::AllocChecker ac;
    if (igd_opregion_.SupportsDp(ddi)) {
        LOG_SPEW("Checking for displayport monitor\n");
        auto dp_disp = fbl::make_unique_checked<DpDisplay>(&ac, this, next_id_, ddi, pipe);
        if (ac.check() && reinterpret_cast<DisplayDevice*>(dp_disp.get())->Init()) {
            return dp_disp;
        }
    }
    if (igd_opregion_.SupportsHdmi(ddi) || igd_opregion_.SupportsDvi(ddi)) {
        LOG_SPEW("Checking for hdmi monitor\n");
        auto hdmi_disp = fbl::make_unique_checked<HdmiDisplay>(&ac, this, next_id_, ddi, pipe);
        if (ac.check() && reinterpret_cast<DisplayDevice*>(hdmi_disp.get())->Init()) {
            return hdmi_disp;
        }
    }

    return nullptr;
}

void Controller::InitDisplays() {
    fbl::AutoLock lock(&display_lock_);
    BringUpDisplayEngine(false);

    for (uint32_t i = 0; i < registers::kDdiCount; i++) {
        auto disp_device = InitDisplay(registers::kDdis[i]);
        if (disp_device) {
            AddDisplay(fbl::move(disp_device));
        }
    }

    if (display_devices_.size() == 0) {
        LOG_INFO("No displays detected\n");
    }

    ReallocatePipeBuffers(false);
}

zx_status_t Controller::AddDisplay(fbl::unique_ptr<DisplayDevice>&& display) {
    fbl::AllocChecker ac;
    display_devices_.reserve(display_devices_.size() + 1, &ac);

    if (ac.check()) {
        display_devices_.push_back(fbl::move(display), &ac);
        assert(ac.check());

        fbl::unique_ptr<DisplayDevice>& new_device = display_devices_[display_devices_.size() - 1];
        LOG_INFO("Display %ld connected (%d x %d, fmt=%08x)\n", new_device->id(),
                 new_device->width(), new_device->height(), new_device->format());
    } else {
        LOG_WARN("Failed to add display device\n");
        return ZX_ERR_NO_MEMORY;
    }

    next_id_++;
    return ZX_OK;
}

// DisplayController methods

void Controller::SetDisplayControllerCb(void* cb_ctx, display_controller_cb_t* cb) {
    acquire_dc_cb_lock();
    dc_cb_ctx_ = cb_ctx;
    _dc_cb_ = cb;

    if (ready_for_callback_) {
        uint64_t displays[registers::kDdiCount];
        uint32_t size;
        {
            fbl::AutoLock lock(&display_lock_);
            size = static_cast<uint32_t>(display_devices_.size());
            for (unsigned i = 0; i < size; i++) {
                displays[i] = display_devices_[i]->id();
            }
        }

        cb->on_displays_changed(cb_ctx, displays, size, NULL, 0);
    }

    release_dc_cb_lock();
}

zx_status_t Controller::GetDisplayInfo(uint64_t display_id, display_info_t* info) {
    DisplayDevice* device;
    fbl::AutoLock lock(&display_lock_);
    if ((device = FindDevice(display_id)) == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }
    info->edid_present = true;
    info->panel.edid.data = device->edid().edid_bytes();
    info->panel.edid.length = device->edid().edid_length();
    info->pixel_formats = supported_formats;
    info->pixel_format_count = static_cast<uint32_t>(fbl::count_of(supported_formats));
    info->cursor_infos = cursor_infos;
    info->cursor_info_count = static_cast<uint32_t>(fbl::count_of(cursor_infos));

    return ZX_OK;
}

zx_status_t Controller::ImportVmoImage(image_t* image, const zx::vmo& vmo, size_t offset) {
    if (!(image->type == IMAGE_TYPE_SIMPLE || image->type == IMAGE_TYPE_X_TILED
                || image->type == IMAGE_TYPE_Y_LEGACY_TILED
                || image->type == IMAGE_TYPE_YF_TILED)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (offset % PAGE_SIZE != 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AutoLock lock(&gtt_lock_);
    fbl::AllocChecker ac;
    imported_images_.reserve(imported_images_.size() + 1, &ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    uint32_t length = width_in_tiles(image->type, image->width, image->pixel_format) *
            height_in_tiles(image->type, image->height, image->pixel_format) *
            get_tile_byte_size(image->type);

    uint32_t align;
    if (image->type == IMAGE_TYPE_SIMPLE) {
        align = registers::PlaneSurface::kLinearAlignment;
    } else if (image->type == IMAGE_TYPE_X_TILED) {
        align = registers::PlaneSurface::kXTilingAlignment;
    } else {
        align = registers::PlaneSurface::kYTilingAlignment;
    }
    fbl::unique_ptr<GttRegion> gtt_region;
    zx_status_t status = gtt_.AllocRegion(length, align, &gtt_region);
    if (status != ZX_OK) {
        return status;
    }

    // The vsync logic requires that images not have base == 0
    if (gtt_region->base() == 0) {
        fbl::unique_ptr<GttRegion> alt_gtt_region;
        zx_status_t status = gtt_.AllocRegion(length, align, &alt_gtt_region);
        if (status != ZX_OK) {
            return status;
        }
        gtt_region = fbl::move(alt_gtt_region);
    }

    status = gtt_region->PopulateRegion(vmo.get(), offset / PAGE_SIZE, length);
    if (status != ZX_OK) {
        return status;
    }

    image->handle = reinterpret_cast<void*>(gtt_region->base());
    imported_images_.push_back(fbl::move(gtt_region));
    return ZX_OK;
}

void Controller::ReleaseImage(image_t* image) {
    fbl::AutoLock lock(&gtt_lock_);
    for (unsigned i = 0; i < imported_images_.size(); i++) {
        if (imported_images_[i]->base() == reinterpret_cast<uint64_t>(image->handle)) {
            imported_images_.erase(i);
            return;
        }
    }
}

const fbl::unique_ptr<GttRegion>& Controller::GetGttRegion(void* handle) {
    fbl::AutoLock lock(&gtt_lock_);
    for (auto& region : imported_images_) {
        if (region->base() == reinterpret_cast<uint64_t>(handle)) {
            return region;
        }
    }
    ZX_ASSERT(false);
}

bool Controller::GetPlaneLayer(registers::Pipe pipe, uint32_t plane,
                               const display_config_t** configs, uint32_t display_count,
                               const layer_t** layer_out) {
    DisplayDevice* disp = nullptr;
    for (auto& d : display_devices_) {
        if (d->pipe() == pipe) {
            disp = d.get();
            break;
        }
    }
    if (disp == nullptr) {
        return false;
    }

    for (unsigned i = 0; i < display_count; i++) {
        const display_config_t* config = configs[i];
        if (config->display_id != disp->id()) {
            continue;
        }
        bool has_color_layer = config->layer_count && config->layers[0]->type == LAYER_COLOR;
        for (unsigned j = 0; j < config->layer_count; j++) {
            if (config->layers[j]->type == LAYER_PRIMARY) {
                if (plane != (config->layers[j]->z_index - has_color_layer)) {
                    continue;
                }
            } else if (config->layers[j]->type == LAYER_CURSOR) {
                // Since the config is validated, we know the cursor is the
                // highest plane, so we don't care about the layer's z_index.
                if (plane != registers::kCursorPlane) {
                    continue;
                }
            } else if (config->layers[j]->type == LAYER_COLOR) {
                // color layers aren't a plane
                continue;
            } else {
                ZX_ASSERT(false);
            }
            *layer_out = config->layers[j];
            return true;
        }
    }
    return false;
}

bool Controller::CalculateMinimumAllocations(const display_config_t** display_configs,
                                             uint32_t display_count,
                                             uint16_t min_allocs[registers::kPipeCount]
                                                                [registers::kImagePlaneCount]) {
    ZX_ASSERT(display_count < registers::kPipeCount);
    // This fn ignores layers after kImagePlaneCount. Displays with too many layers already
    // failed in ::CheckConfiguration, so it doesn't matter if we incorrectly say they pass here.

    bool success = true;
    for (unsigned pipe_num = 0; pipe_num < registers::kPipeCount; pipe_num++) {
        registers::Pipe pipe = registers::kPipes[pipe_num];
        uint32_t total = 0;

        for (unsigned plane_num = 0; plane_num < registers::kImagePlaneCount; plane_num++) {
            const layer_t* layer;
            if (!GetPlaneLayer(pipe, plane_num, display_configs, display_count, &layer)) {
                min_allocs[pipe_num][plane_num] = 0;
                continue;
            }

            if (layer->type == LAYER_CURSOR) {
                min_allocs[pipe_num][plane_num] = 8;
                continue;
            }

            ZX_ASSERT(layer->type == LAYER_PRIMARY);
            const primary_layer_t* primary = &layer->cfg.primary;

            if (primary->image.type == IMAGE_TYPE_SIMPLE
                    || primary->image.type == IMAGE_TYPE_X_TILED) {
                min_allocs[pipe_num][plane_num] = 8;
            } else {
                uint32_t plane_source_width;
                uint32_t min_scan_lines;
                uint32_t bytes_per_pixel = ZX_PIXEL_FORMAT_BYTES(primary->image.pixel_format);
                if (primary->transform_mode == FRAME_TRANSFORM_IDENTITY
                        || primary->transform_mode == FRAME_TRANSFORM_ROT_180) {
                    plane_source_width = primary->src_frame.width;
                    min_scan_lines = 8;
                } else {
                    plane_source_width = primary->src_frame.height;
                    min_scan_lines = 32 / bytes_per_pixel;
                }
                min_allocs[pipe_num][plane_num] = static_cast<uint16_t>(
                        ((fbl::round_up(4u * plane_source_width * bytes_per_pixel, 512u) / 512u) *
                        (min_scan_lines / 4)) + 3);
                if (min_allocs[pipe_num][plane_num] < 8) {
                    min_allocs[pipe_num][plane_num] = 8;
                }

            }
            total += min_allocs[pipe_num][plane_num];
        }

        ZX_ASSERT(pipe_buffers_[pipe_num].end >= pipe_buffers_[pipe_num].start);
        if (total > static_cast<uint16_t>(
                pipe_buffers_[pipe_num].end - pipe_buffers_[pipe_num].start)) {
            min_allocs[pipe_num][0] = UINT16_MAX;
            success = false;
        }
    }

    return success;
}

void Controller::UpdateAllocations(const uint16_t min_allocs[registers::kPipeCount]
                                                            [registers::kImagePlaneCount],
                                   const uint64_t data_rate[registers::kPipeCount]
                                                           [registers::kImagePlaneCount]) {
    uint16_t allocs[registers::kPipeCount][registers::kImagePlaneCount];

    for (unsigned pipe_num = 0; pipe_num < registers::kPipeCount; pipe_num++) {
        uint64_t total_data_rate = 0;
        for (unsigned plane_num = 0; plane_num < registers::kImagePlaneCount; plane_num++) {
            total_data_rate += data_rate[pipe_num][plane_num];
        }
        if (total_data_rate == 0) {
            for (unsigned plane_num = 0; plane_num < registers::kImagePlaneCount; plane_num++) {
                allocs[pipe_num][plane_num] = 0;
            }
            continue;
        }

        // Allocate buffers based on the percentage of the total pixel bandwidth they take. If
        // that percentage isn't enough for a plane, give that plane its minimum allocation and
        // then try again.
        double buffers_per_pipe = pipe_buffers_[pipe_num].end - pipe_buffers_[pipe_num].start;
        bool forced_alloc[registers::kImagePlaneCount] = {};
        bool done = false;
        while (!done) {
            for (unsigned plane_num = 0; plane_num < registers::kImagePlaneCount; plane_num++) {
                if (forced_alloc[plane_num]) {
                    continue;
                }

                double blocks = buffers_per_pipe *
                        static_cast<double>(data_rate[pipe_num][plane_num]) /
                        static_cast<double>(total_data_rate);
                allocs[pipe_num][plane_num] = static_cast<uint16_t>(blocks);
            }

            done = true;

            for (unsigned plane_num = 0; plane_num < registers::kImagePlaneCount; plane_num++) {
                if (allocs[pipe_num][plane_num] < min_allocs[pipe_num][plane_num]) {
                    done = false;
                    allocs[pipe_num][plane_num] = min_allocs[pipe_num][plane_num];
                    forced_alloc[plane_num] = true;
                    total_data_rate -= data_rate[pipe_num][plane_num];
                    buffers_per_pipe -= allocs[pipe_num][plane_num];
                }
            }
        }
    }

    // Do the actual allocation, using the buffers that are asigned to each pipe.
    for (unsigned pipe_num = 0; pipe_num < registers::kPipeCount; pipe_num++) {
        uint16_t start = pipe_buffers_[pipe_num].start;
        for (unsigned plane_num = 0; plane_num < registers::kImagePlaneCount; plane_num++) {
            auto cur = &plane_buffers_[pipe_num][plane_num];
            cur->minimum = min_allocs[pipe_num][plane_num];

            if (allocs[pipe_num][plane_num] == 0) {
                cur->start = registers::PlaneBufCfg::kBufferCount;
                cur->end = static_cast<uint16_t>(cur->start + 1);
            } else {
                cur->start = start;
                cur->end = static_cast<uint16_t>(start + allocs[pipe_num][plane_num]);
            }
            start = static_cast<uint16_t>(start + allocs[pipe_num][plane_num]);

            registers::Pipe pipe = registers::kPipes[pipe_num];
            registers::PipeRegs pipe_regs(pipe);

            // These are latched on the surface address register, so we don't yet need to
            // worry about overlaps when updating planes during a pipe allocation.
            auto buf_cfg = pipe_regs.PlaneBufCfg(plane_num + 1).FromValue(0);
            buf_cfg.set_buffer_start(cur->start);
            buf_cfg.set_buffer_end(cur->end - 1);
            buf_cfg.WriteTo(mmio_space());

            // TODO(stevensd): Real watermark programming
            auto wm0 = pipe_regs.PlaneWatermark(plane_num + 1, 0).FromValue(0);
            wm0.set_enable(cur->start != registers::PlaneBufCfg::kBufferCount);
            wm0.set_blocks(cur->end - cur->start);
            wm0.WriteTo(mmio_space());

            // Give the buffers to both the cursor plane and plane 2, since
            // only one will actually be active.
            if (plane_num == registers::kCursorPlane) {
                auto buf_cfg = pipe_regs.PlaneBufCfg(0).FromValue(0);
                buf_cfg.set_buffer_start(cur->start);
                buf_cfg.set_buffer_end(cur->end - 1);
                buf_cfg.WriteTo(mmio_space());

                auto wm0 = pipe_regs.PlaneWatermark(0, 0).FromValue(0);
                wm0.set_enable(cur->start != registers::PlaneBufCfg::kBufferCount);
                wm0.set_blocks(cur->end - cur->start);
                wm0.WriteTo(mmio_space());
            }
        }
    }
}

bool Controller::ReallocatePlaneBuffers(const display_config_t** display_configs,
                                        uint32_t display_count) {
    uint16_t min_allocs[registers::kPipeCount][registers::kImagePlaneCount];
    if (!CalculateMinimumAllocations(display_configs, display_count, min_allocs)) {
        return false;
    }

    // Calculate the data rates and store the minimum allocations
    uint64_t data_rate[registers::kPipeCount][registers::kImagePlaneCount];
    for (unsigned pipe_num = 0; pipe_num < registers::kPipeCount; pipe_num++) {
        registers::Pipe pipe = registers::kPipes[pipe_num];
        for (unsigned plane_num = 0; plane_num < registers::kImagePlaneCount; plane_num++) {
            const layer_t* layer;
            if (!GetPlaneLayer(pipe, plane_num, display_configs, display_count, &layer)) {
                data_rate[pipe_num][plane_num] = 0;
            } else if (layer->type == LAYER_PRIMARY) {
                const primary_layer_t* primary = &layer->cfg.primary;

                uint32_t scaled_width = primary->src_frame.width * primary->src_frame.width
                        / primary->dest_frame.width;
                uint32_t scaled_height = primary->src_frame.height * primary->src_frame.height
                        / primary->dest_frame.height;
                data_rate[pipe_num][plane_num] = scaled_width * scaled_height *
                        ZX_PIXEL_FORMAT_BYTES(primary->image.pixel_format);
            } else if (layer->type == LAYER_CURSOR) {
                // Use a tiny data rate so the cursor gets the minimum number of buffers
                data_rate[pipe_num][plane_num] = 1;
            } else {
                // Other layers don't use pipe/planes, so GetPlaneLayer should have returned false
                ZX_ASSERT(false);
            }
        }
    }

    // It's not necessary to flush the buffer changes since the pipe allocs didn't change
    UpdateAllocations(min_allocs, data_rate);
    return true;
}

void Controller::ReallocatePipeBuffers(bool is_hotplug) {
    if (display_devices_.size() == 0) {
        // We'll reallocate things when there's actually a display
        return;
    }

    // TODO(stevensd): Separate pipe allocation for displays being connected
    bool realloc_fail = false;
    uint16_t buffers_per_pipe = static_cast<uint16_t>(registers::PlaneBufCfg::kBufferCount /
        fbl::min(display_devices_.size(), static_cast<size_t>(registers::kPipeCount)));

    // Approximate the data rate based on how many buffers are allocated to each plane. This
    // can be slightly off, but that'll be fixed on the next page flip.
    uint16_t min_allocs[registers::kPipeCount][registers::kImagePlaneCount];
    uint64_t data_rate[registers::kPipeCount][registers::kImagePlaneCount];
    for (unsigned pipe_num = 0; pipe_num < registers::kPipeCount; pipe_num++) {
        uint16_t pipe_total = 0;
        for (unsigned plane_num = 0; plane_num < registers::kImagePlaneCount; plane_num++) {
            auto alloc = &plane_buffers_[pipe_num][plane_num];
            data_rate[pipe_num][plane_num] = alloc->start == registers::PlaneBufCfg::kBufferCount ?
                    0 : alloc->end - alloc->start;
            min_allocs[pipe_num][plane_num] = alloc->minimum;
            pipe_total = static_cast<uint16_t>(pipe_total + alloc->minimum);
        }
        if (pipe_total > buffers_per_pipe) {
            realloc_fail = true;
        }
    }

    // If we can't reallocate anything, disable all the planes and wait for a page flip due
    // to the client handling the hotplug. This will cause the displays to flash, but bad
    // hotplugs like this should be uncommon. This shouldn't happen with the virtcon, since
    // its buffer requirements are really low, so waiting for a flip is okay.
    if (realloc_fail) {
        ZX_DEBUG_ASSERT(is_hotplug);

        LOG_INFO("Cannot reallocate buffers for hot plug\n");
        for (unsigned pipe_num = 0; pipe_num < registers::kPipeCount; pipe_num++) {
            registers::Pipe pipe = registers::kPipes[pipe_num];
            registers::PipeRegs pipe_regs(pipe);
            for (unsigned plane_num = 0; plane_num < registers::kImagePlaneCount; plane_num++) {
                 pipe_regs.PlaneControl(plane_num).ReadFrom(mmio_space())
                        .set_plane_enable(0).WriteTo(mmio_space());
                 pipe_regs.PlaneSurface(plane_num).ReadFrom(mmio_space()).WriteTo(mmio_space());
            }
            pipe_regs.CursorBase().ReadFrom(mmio_space()).WriteTo(mmio_space());
        }
        return;
    }

    // Allocate buffers to each pipe, but save the old one for use later.
    pipe_buffer_allocation_t active_allocation[registers::kPipeCount];
    memcpy(active_allocation, pipe_buffers_, sizeof(active_allocation));
    int active_pipes = 0;
    for (unsigned pipe_num = 0; pipe_num < registers::kPipeCount; pipe_num++) {
        bool found = false;
        for (auto& display : display_devices_) {
            if (display->pipe() == pipe_num) {
                found = true;
                break;
            }
        }
        if (found) {
            pipe_buffers_[pipe_num].start = static_cast<uint16_t>(buffers_per_pipe * active_pipes);
            pipe_buffers_[pipe_num].end = static_cast<uint16_t>(
                    pipe_buffers_[pipe_num].start + buffers_per_pipe);
            active_pipes++;
        } else {
            pipe_buffers_[pipe_num].start = pipe_buffers_[pipe_num].end = 0;
        }
        LOG_SPEW("Pipe %d buffers: [%d, %d)\n", pipe_num,
                 pipe_buffers_[pipe_num].start, pipe_buffers_[pipe_num].end);
    }

    UpdateAllocations(min_allocs, data_rate);

    // If it's not a hotplug, we weren't using anything before so we don't need to
    // worry about allocations overlapping.
    if (!is_hotplug) {
        return;
    }

    // Given that the order of the allocations is fixed, an allocation X_i is contained completely
    // within its old allocation if {new len of allocations preceding X_i} >= {start of old X_i} and
    // {new len of allocations preceding X_i + new len of X_i} <= {end of old X_i}. For any i,
    // if condition 1 holds, either condition 2 is true and we're done, or condition 2 doesn't
    // and condition 1 holds for i + 1. Since condition 1 holds for i == 0 and because condition
    // 2 holds for the last allocation (since the allocation is valid), it is guaranteed that
    // at least one allocation is entirely within its old allocation. The remaining buffers
    // are guaranteed to be re-allocatable recursively in the same manner. Therefore the loop will
    // make progress every iteration.
    bool done = false;
    while (!done) {
        done = true;
        for (unsigned pipe_num = 0; pipe_num < registers::kPipeCount; pipe_num++) {
            auto active_alloc = active_allocation + pipe_num;
            auto goal_alloc = pipe_buffers_ + pipe_num;

            if (active_alloc->start == goal_alloc->start &&
                    active_alloc->end == goal_alloc->end) {
                continue;
            }

            // Look through all the other active pipe allocations for overlap
            bool overlap = false;
            if (goal_alloc->start != goal_alloc->end) {
                for (unsigned other_pipe = 0; other_pipe < registers::kPipeCount; other_pipe++) {
                    if (other_pipe == pipe_num) {
                        continue;
                    }

                    auto other_active = active_allocation + other_pipe;
                    if (other_active->start == other_active->end) {
                        continue;
                    }

                    if ((other_active->start <= goal_alloc->start
                                && goal_alloc->start < other_active->end)
                            || (other_active->start < goal_alloc->end
                                && goal_alloc->end <= other_active->end)) {
                        overlap = true;
                        break;
                    }
                }
            }

            if (!overlap) {
                // Flush the pipe allocation, wait for it to be active, and update
                // what is current active.
                registers::PipeRegs pipe_regs(registers::kPipes[pipe_num]);
                for (unsigned j = 0; j < registers::kImagePlaneCount; j++) {
                    pipe_regs.PlaneSurface(j).ReadFrom(mmio_space()).WriteTo(mmio_space());
                }
                pipe_regs.CursorBase().ReadFrom(mmio_space()).WriteTo(mmio_space());

                // TODO(stevensd): Wait for vsync instead of sleeping
                // TODO(stevesnd): Parallelize/reduce the number of vsyncs we wait for
                zx_nanosleep(zx_deadline_after(ZX_MSEC(33)));

                *active_alloc = *goal_alloc;
            } else {
                done = false;
            }
        }
    }
}

bool Controller::CheckDisplayLimits(const display_config_t** display_configs,
                                    uint32_t display_count) {
    for (unsigned i = 0; i < display_count; i++) {
        const display_config_t* config = display_configs[i];
        DisplayDevice* display = FindDevice(config->display_id);
        if (display == nullptr) {
            continue;
        }

        // TODO(stevensd): The current display limits check only check that the mode is
        // supported - it also needs to check that the layer configuration is supported,
        // and return layer errors if it isn't.
        // TODO(stevensd): Check maximum memory read bandwidth, watermark

        if (config->mode.h_addressable > 4096 || config->mode.v_addressable > 8192
                || !display->CheckDisplayLimits(config)) {
            // The API guarantees that if there are multiple displays, then each
            // display is supported in isolation. Debug assert if that's violated.
            ZX_DEBUG_ASSERT(display_count == 1);
            return false;
        }
    }

    return true;
}

void Controller::CheckConfiguration(const display_config_t** display_config,
                                    uint32_t* display_cfg_result, uint32_t** layer_cfg_result,
                                    uint32_t display_count) {
    if (display_count > registers::kPipeCount) {
        *display_cfg_result = CONFIG_DISPLAY_TOO_MANY;
        return;
    }

    fbl::AutoLock lock(&display_lock_);

    if (display_count == 0) {
        // All displays off is supported
        *display_cfg_result = CONFIG_DISPLAY_OK;
        return;
    }

    if (!CheckDisplayLimits(display_config, display_count)) {
        *display_cfg_result = CONFIG_DISPLAY_UNSUPPORTED_MODES;
        return;
    }

    *display_cfg_result = CONFIG_DISPLAY_OK;
    for (unsigned i = 0; i < display_count; i++) {
        auto* config = display_config[i];
        DisplayDevice* display = nullptr;
        for (auto& d : display_devices_) {
            if (d->id() == config->display_id) {
                display = d.get();
                break;
            }
        }
        if (display == nullptr) {
            LOG_INFO("Got config with no display - assuming hotplug and skipping");
            continue;
        }

        bool merge_all = false;
        if (config->layer_count > 3) {
            merge_all = config->layer_count > 4 || config->layers[0]->type != LAYER_COLOR;
        }
        if (!merge_all && config->cc_flags) {
            if (config->cc_flags & COLOR_CONVERSION_PREOFFSET) {
                for (int i = 0; i < 3; i++) {
                    merge_all |= config->cc_preoffsets[i] <= -1;
                    merge_all |= config->cc_preoffsets[i] >= 1;
                }
            }
            if (config->cc_flags & COLOR_CONVERSION_POSTOFFSET) {
                for (int i = 0; i < 3; i++) {
                    merge_all |= config->cc_postoffsets[i] <= -1;
                    merge_all |= config->cc_postoffsets[i] >= 1;
                }
            }
        }

        if (merge_all) {
            layer_cfg_result[i][0] = CLIENT_MERGE_BASE;
            for (unsigned j = 1; j < config->layer_count; j++) {
                layer_cfg_result[i][j] = CLIENT_MERGE_SRC;
            }
            continue;
        }

        uint32_t total_scalers_needed = 0;
        for (unsigned j = 0; j < config->layer_count; j++) {
            switch (config->layers[j]->type) {
            case LAYER_PRIMARY: {
                primary_layer_t* primary = &config->layers[j]->cfg.primary;
                if (primary->transform_mode == FRAME_TRANSFORM_ROT_90
                        || primary->transform_mode == FRAME_TRANSFORM_ROT_270) {
                    // Linear and x tiled images don't support 90/270 rotation
                    if (primary->image.type == IMAGE_TYPE_SIMPLE
                            || primary->image.type == IMAGE_TYPE_X_TILED) {
                        layer_cfg_result[i][j] |= CLIENT_TRANSFORM;
                    }
                } else if (primary->transform_mode != FRAME_TRANSFORM_IDENTITY
                        && primary->transform_mode != FRAME_TRANSFORM_ROT_180) {
                    // Cover unsupported rotations
                    layer_cfg_result[i][j] |= CLIENT_TRANSFORM;
                }

                uint32_t src_width;
                uint32_t src_height;
                if (primary->transform_mode == FRAME_TRANSFORM_IDENTITY
                        || primary->transform_mode == FRAME_TRANSFORM_ROT_180
                        || primary->transform_mode == FRAME_TRANSFORM_REFLECT_X
                        || primary->transform_mode == FRAME_TRANSFORM_REFLECT_Y) {
                    src_width = primary->src_frame.width;
                    src_height = primary->src_frame.height;
                } else {
                    src_width = primary->src_frame.height;
                    src_height = primary->src_frame.width;
                }

                if (primary->dest_frame.width != src_width
                        || primary->dest_frame.height != src_height) {
                    float ratio = registers::PipeScalerCtrl::k7x5MaxRatio;
                    uint32_t max_width =
                            static_cast<uint32_t>(static_cast<float>(src_width) * ratio);
                    uint32_t max_height =
                            static_cast<uint32_t>(static_cast<float>(src_height) * ratio);
                    uint32_t scalers_needed = 1;
                    // The 7x5 scaler (i.e. 2 scaler resources) is required if the src width is
                    // >2048 and the required vertical scaling is greater than 1.99.
                    if (primary->src_frame.width > 2048) {
                        float ratio = registers::PipeScalerCtrl::kDynamicMaxVerticalRatio2049;
                        uint32_t max_dynamic_height =
                                static_cast<uint32_t>(static_cast<float>(src_height) * ratio);
                        if (max_dynamic_height < primary->dest_frame.height) {
                            scalers_needed = 2;
                        }
                    }
                    
                    // Verify that there are enough scaler resources
                    // Verify that the scaler input isn't too large or too small
                    // Verify that the required scaling ratio isn't too large
                    if ((total_scalers_needed + scalers_needed) >
                            (display->pipe() == registers::PIPE_C
                                ? registers::PipeScalerCtrl::kPipeCScalersAvailable
                                : registers::PipeScalerCtrl::kPipeABScalersAvailable)
                            || src_width > registers::PipeScalerCtrl::kMaxSrcWidthPx
                            || src_width < registers::PipeScalerCtrl::kMinSrcSizePx
                            || src_height < registers::PipeScalerCtrl::kMinSrcSizePx
                            || max_width < primary->dest_frame.width
                            || max_height < primary->dest_frame.height) {   
                        layer_cfg_result[i][j] |= CLIENT_FRAME_SCALE;
                    } else {
                        total_scalers_needed += scalers_needed;
                    }
                }
                break;
            }
            case LAYER_CURSOR: {
                if (j != config->layer_count - 1) {
                    layer_cfg_result[i][j] |= CLIENT_USE_PRIMARY;
                }
                const image_t* image = &config->layers[j]->cfg.cursor.image;
                if (image->type != IMAGE_TYPE_SIMPLE) {
                    layer_cfg_result[i][j] |= CLIENT_USE_PRIMARY;
                }
                bool found = false;
                for (unsigned x = 0; x < fbl::count_of(cursor_infos) && !found; x++) {
                    found = image->width == cursor_infos[x].width
                            && image->height == cursor_infos[x].height
                            && image->pixel_format == cursor_infos[x].format;
                }
                if (!found) {
                    layer_cfg_result[i][j] |= CLIENT_USE_PRIMARY;
                }
                break;
            }
            case LAYER_COLOR: {
                if (j != 0) {
                    layer_cfg_result[i][j] |= CLIENT_USE_PRIMARY;
                }
                zx_pixel_format_t format = config->layers[j]->cfg.color.format;
                if (format != ZX_PIXEL_FORMAT_RGB_x888
                        && format != ZX_PIXEL_FORMAT_ARGB_8888) {
                    layer_cfg_result[i][j] |= CLIENT_USE_PRIMARY;
                }
                break;
            }
            default:
                layer_cfg_result[i][j] |= CLIENT_USE_PRIMARY;
            }
        }
    }

    // CalculateMinimumAllocations ignores layers after kImagePlaneCount. That's fine, since
    // that case already fails from an earlier check.
    uint16_t arr[registers::kPipeCount][registers::kImagePlaneCount];
    if (!CalculateMinimumAllocations(display_config, display_count, arr)) {
        // Find any displays whose allocation fails and set the return code. Overwrite
        // any previous errors, since they get solved by the merge.
        for (unsigned pipe_num = 0; pipe_num < registers::kPipeCount; pipe_num++) {
            if (arr[pipe_num][0] != UINT16_MAX) {
                continue;
            }
            for (auto& display : display_devices_) {
                if (display->pipe() != pipe_num) {
                    continue;
                }

                for (unsigned i = 0; i < display_count; i++) {
                    if (display_config[i]->display_id != display->id()) {
                        continue;
                    }
                    layer_cfg_result[i][0] = CLIENT_MERGE_BASE;
                    for (unsigned j = 1; j < display_config[i]->layer_count; j++) {
                        layer_cfg_result[i][j] = CLIENT_MERGE_SRC;
                    }
                    break;
                }
            }
        }
    }
}

void Controller::ApplyConfiguration(const display_config_t** display_config,
                                    uint32_t display_count) {
    uint64_t fake_vsyncs[registers::kDdiCount];
    uint32_t fake_vsync_count = 0;

    {
        fbl::AutoLock lock(&display_lock_);

        // If we reallocated the pipe allocations since things were validated, then this
        // can fail. In that case, just wait for the client to respond to the hotplug event.
        if (!ReallocatePlaneBuffers(display_config, display_count)) {
            return;
        }

        for (auto& display : display_devices_) {
            const display_config_t* config = nullptr;
            for (unsigned i = 0; i < display_count; i++) {
                if (display_config[i]->display_id == display->id()) {
                    config = display_config[i];
                    break;
                }
            }

            if (config == nullptr) {
                display->ClearConfig();
            } else {
                registers::pipe_arming_regs_t regs;

                display->ApplyConfiguration(config, &regs);

                registers::PipeRegs pipe_regs(display->pipe());
                pipe_regs.CscMode().FromValue(regs.csc_mode).WriteTo(mmio_space());
                pipe_regs.PipeBottomColor().FromValue(regs.pipe_bottom_color).WriteTo(mmio_space());
                pipe_regs.CursorBase().FromValue(regs.cur_base).WriteTo(mmio_space());
                pipe_regs.CursorPos().FromValue(regs.cur_pos).WriteTo(mmio_space());
                for (unsigned i = 0; i < registers::kImagePlaneCount; i++) {
                    pipe_regs.PlaneSurface(i).FromValue(regs.plane_surf[i]).WriteTo(mmio_space());
                }
                pipe_regs.PipeScalerWinSize(0).FromValue(regs.ps_win_sz[0]).WriteTo(mmio_space());
                if (display->pipe() != registers::PIPE_C) {
                    pipe_regs.PipeScalerWinSize(1)
                            .FromValue(regs.ps_win_sz[1]).WriteTo(mmio_space());
                }
            }

            // The hardware only gives vsyncs if at least one plane is enabled, so
            // fake one if we need to, to inform the client that we're done with the
            // images.
            if (!config || config->layer_count == 0) {
                fake_vsyncs[fake_vsync_count++] = display->id();
            }
        }
    }

    acquire_dc_cb_lock();
    if (dc_cb()) {
        zx_time_t now = fake_vsync_count ? zx_clock_get(ZX_CLOCK_MONOTONIC) : 0;
        for (unsigned i = 0; i < fake_vsync_count; i++) {
            dc_cb()->on_display_vsync(dc_cb_ctx_, fake_vsyncs[i], now, nullptr, 0);
        }
    }
    release_dc_cb_lock();
}

uint32_t Controller::ComputeLinearStride(uint32_t width, zx_pixel_format_t format) {
    return fbl::round_up(width,
            get_tile_byte_width(IMAGE_TYPE_SIMPLE, format) / ZX_PIXEL_FORMAT_BYTES(format));
}

zx_status_t Controller::AllocateVmo(uint64_t size, zx_handle_t* vmo_out) {
    return zx_vmo_create(size, 0, vmo_out);
}

// Intel GPU core methods

zx_status_t Controller::ReadPciConfig16(uint16_t addr, uint16_t* value_out) {
    return pci_config_read16(&pci_, addr, value_out);
}

zx_status_t Controller::MapPciMmio(uint32_t pci_bar, void** addr_out, uint64_t* size_out) {
    if (pci_bar > PCI_MAX_BAR_COUNT) {
        return ZX_ERR_INVALID_ARGS;
    }
    fbl::AutoLock lock(&bar_lock_);
    if (mapped_bars_[pci_bar].count == 0) {
        zx_status_t status = pci_map_bar(&pci_, pci_bar, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                         &mapped_bars_[pci_bar].base,
                                         &mapped_bars_[pci_bar].size,
                                         &mapped_bars_[pci_bar].vmo);
        if (status != ZX_OK) {
            return status;
        }
    }
    *addr_out = mapped_bars_[pci_bar].base;
    *size_out = mapped_bars_[pci_bar].size;
    mapped_bars_[pci_bar].count++;
    return ZX_OK;
}

zx_status_t Controller::UnmapPciMmio(uint32_t pci_bar) {
    if (pci_bar > PCI_MAX_BAR_COUNT) {
        return ZX_ERR_INVALID_ARGS;
    }
    fbl::AutoLock lock(&bar_lock_);
    if (mapped_bars_[pci_bar].count == 0) {
        return ZX_OK;
    }
    if (--mapped_bars_[pci_bar].count == 0) {
        zx_vmar_unmap(zx_vmar_root_self(),
                      reinterpret_cast<uintptr_t>(mapped_bars_[pci_bar].base),
                      mapped_bars_[pci_bar].size);
        zx_handle_close(mapped_bars_[pci_bar].vmo);
    }
    return ZX_OK;
}

zx_status_t Controller::GetPciBti(uint32_t index, zx_handle_t* bti_out) {
    return pci_get_bti(&pci_, index, bti_out);
}

zx_status_t Controller::RegisterInterruptCallback(zx_intel_gpu_core_interrupt_callback_t callback,
                                                  void* data, uint32_t interrupt_mask) {
    return interrupts_.SetInterruptCallback(callback, data, interrupt_mask);
}

zx_status_t Controller::UnregisterInterruptCallback() {
    interrupts_.SetInterruptCallback(nullptr, nullptr, 0);
    return ZX_OK;
}

uint64_t Controller::GttGetSize() {
    fbl::AutoLock lock(&gtt_lock_);
    return gtt_.size();
}

zx_status_t Controller::GttAlloc(uint64_t page_count, uint64_t* addr_out) {
    uint64_t length = page_count * PAGE_SIZE;
    fbl::AutoLock lock(&gtt_lock_);
    if (length > gtt_.size()) {
        return ZX_ERR_INVALID_ARGS;
    }
    fbl::unique_ptr<GttRegion> region;
    zx_status_t status = gtt_.AllocRegion(static_cast<uint32_t>(page_count * PAGE_SIZE),
                                          PAGE_SIZE, &region);
    if (status != ZX_OK) {
        return status;
    }
    *addr_out = region->base();

    imported_gtt_regions_.push_back(fbl::move(region));
    return ZX_OK;
}

zx_status_t Controller::GttFree(uint64_t addr) {
    fbl::AutoLock lock(&gtt_lock_);
    for (unsigned i = 0; i < imported_gtt_regions_.size(); i++) {
        if (imported_gtt_regions_[i]->base() == addr) {
            imported_gtt_regions_.erase(i)->ClearRegion(true);
            return ZX_OK;
        }
    }
    return ZX_ERR_INVALID_ARGS;
}

zx_status_t Controller::GttClear(uint64_t addr) {
    fbl::AutoLock lock(&gtt_lock_);
    for (unsigned i = 0; i < imported_gtt_regions_.size(); i++) {
        if (imported_gtt_regions_[i]->base() == addr) {
            imported_gtt_regions_[i]->ClearRegion(true);
            return ZX_OK;
        }
    }
    return ZX_ERR_INVALID_ARGS;
}

zx_status_t Controller::GttInsert(uint64_t addr, zx_handle_t buffer,
                                  uint64_t page_offset, uint64_t page_count) {
    fbl::AutoLock lock(&gtt_lock_);
    for (unsigned i = 0; i < imported_gtt_regions_.size(); i++) {
        if (imported_gtt_regions_[i]->base() == addr) {
            return imported_gtt_regions_[i]->PopulateRegion(buffer, page_offset,
                                                            page_count * PAGE_SIZE,
                                                            true /* writable */);
        }
    }
    return ZX_ERR_INVALID_ARGS;
}

void Controller::GpuRelease() {
    gpu_released_ = true;
    if (display_released_) {
        delete this;
    }
}

// Ddk methods

void Controller::DdkUnbind() {
    device_remove(zxdev());
    device_remove(zx_gpu_dev_);

    fbl::AutoLock lock(&display_lock_);
    display_devices_.reset();
}

void Controller::DdkRelease() {
    display_released_ = true;
    if (gpu_released_) {
        delete this;
    }
}

zx_status_t Controller::DdkSuspend(uint32_t hint) {
    if ((hint & DEVICE_SUSPEND_REASON_MASK) == DEVICE_SUSPEND_FLAG_MEXEC) {
        uint32_t format, width, height, stride;
        if (zx_framebuffer_get_info(get_root_resource(), &format, &width,
                                    &height, &stride) != ZX_OK) {
            return ZX_OK;
        }

        // The bootloader framebuffer is most likely at the start of the display
        // controller's bar 2. Try to get that buffer working again across the
        // mexec by mapping gfx stolen memory to gaddr 0.

        auto bdsm_reg = registers::BaseDsm::Get().FromValue(0);
        zx_status_t status =
                pci_config_read32(&pci_, bdsm_reg.kAddr, bdsm_reg.reg_value_ptr());
        if (status != ZX_OK) {
            LOG_TRACE("Failed to read dsm base\n");
            return ZX_OK;
        }

        // The Intel docs say that the first page should be reserved for the gfx
        // hardware, but a lot of BIOSes seem to ignore that.
        uintptr_t fb = bdsm_reg.base_phys_addr() << bdsm_reg.base_phys_addr_shift;
        uint32_t fb_size = stride * height * ZX_PIXEL_FORMAT_BYTES(format);

        {
            fbl::AutoLock lock(&gtt_lock_);
            gtt_.SetupForMexec(fb, fb_size);
        }

        // Try to map the framebuffer and clear it. If not, oh well.
        void* gmadr;
        uint64_t gmadr_size;
        zx_handle_t gmadr_handle;
        if (pci_map_bar(&pci_, 2, ZX_CACHE_POLICY_WRITE_COMBINING,
                        &gmadr, &gmadr_size, &gmadr_handle) == ZX_OK) {
            memset(reinterpret_cast<void*>(gmadr), 0, fb_size);
            zx_handle_close(gmadr_handle);
        }

        {
            fbl::AutoLock lock(&display_lock_);
            for (auto& display : display_devices_) {
                // TODO(ZX-1413): Reset/scale the display to ensure the buffer displays properly
                registers::PipeRegs pipe_regs(display->pipe());

                auto plane_stride = pipe_regs.PlaneSurfaceStride(0).ReadFrom(mmio_space_.get());
                plane_stride.set_stride(width_in_tiles(IMAGE_TYPE_SIMPLE, width, format));
                plane_stride.WriteTo(mmio_space_.get());

                auto plane_surface = pipe_regs.PlaneSurface(0).ReadFrom(mmio_space_.get());
                plane_surface.set_surface_base_addr(0);
                plane_surface.WriteTo(mmio_space_.get());
            }
        }
    }
    return ZX_OK;
}

zx_status_t Controller::DdkResume(uint32_t hint) {
    fbl::AutoLock lock(&display_lock_);
    BringUpDisplayEngine(true);

    registers::PanelPowerDivisor::Get().FromValue(pp_divisor_val_).WriteTo(mmio_space_.get());
    registers::PanelPowerOffDelay::Get().FromValue(pp_off_delay_val_).WriteTo(mmio_space_.get());
    registers::PanelPowerOnDelay::Get().FromValue(pp_on_delay_val_).WriteTo(mmio_space_.get());
    registers::SouthBacklightCtl1::Get().FromValue(0)
            .set_polarity(sblc_polarity_).WriteTo(mmio_space_.get());
    registers::SouthBacklightCtl2::Get().FromValue(sblc_ctrl2_val_).WriteTo(mmio_space_.get());
    registers::SChicken1::Get().FromValue(schicken1_val_).WriteTo(mmio_space_.get());

    registers::DdiRegs(registers::DDI_A).DdiBufControl().ReadFrom(mmio_space_.get())
            .set_ddi_a_lane_capability_control(ddi_a_lane_capability_control_)
            .WriteTo(mmio_space_.get());

    for (auto& disp : display_devices_) {
        if (!disp->Resume()) {
            LOG_ERROR("Failed to resume display\n");
        }
    }

    interrupts_.Resume();

    ReallocatePipeBuffers(false);

    return ZX_OK;
}

// TODO(stevensd): Move this back into ::Bind once long-running binds don't
// break devmgr's suspend/mexec.
void Controller::FinishInit() {
    LOG_TRACE("i915: initializing displays\n");
    InitDisplays();

    acquire_dc_cb_lock();
    uint64_t displays[registers::kDdiCount];
    uint32_t size = 0;
    {
        fbl::AutoLock lock(&display_lock_);
        if (display_devices_.size()) {
            size = static_cast<uint32_t>(display_devices_.size());
            for (unsigned i = 0; i < size; i++) {
                displays[i] = display_devices_[i]->id();
            }
        }
    }

    if (dc_cb() && size) {
        dc_cb()->on_displays_changed(dc_cb_ctx_, displays, size, NULL, 0);
    }

    ready_for_callback_ = true;
    release_dc_cb_lock();

    interrupts_.FinishInit();

    // TODO remove when the gfxconsole moves to user space
    EnableBacklight(true);

    LOG_TRACE("i915: initialization done\n");
}

zx_status_t Controller::Bind(fbl::unique_ptr<i915::Controller>* controller_ptr) {
    LOG_TRACE("Binding to display controller\n");

    if (device_get_protocol(parent_, ZX_PROTOCOL_PCI, &pci_)) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    pci_config_read16(&pci_, PCI_CONFIG_DEVICE_ID, &device_id_);
    LOG_TRACE("Device id %x\n", device_id_);
    if (device_id_ == INTEL_I915_BROADWELL_DID) {
        // TODO: this should be based on the specific target
        flags_ |= FLAGS_BACKLIGHT;
    }

    zx_status_t status = igd_opregion_.Init(&pci_);
    if (status != ZX_OK) {
        LOG_ERROR("Failed to init VBT (%d)\n", status);
        return status;
    }

    LOG_TRACE("Mapping registers\n");
    // map register window
    void* regs;
    uint64_t size;
    status = MapPciMmio(0u, &regs, &size);
    if (status != ZX_OK) {
        LOG_ERROR("Failed to map bar 0: %d\n", status);
        return status;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<hwreg::RegisterIo> mmio_space(
            new (&ac) hwreg::RegisterIo(reinterpret_cast<volatile void*>(regs)));
    if (!ac.check()) {
        LOG_ERROR("Failed to alloc RegisterIo\n");
        return ZX_ERR_NO_MEMORY;
    }
    mmio_space_ = fbl::move(mmio_space);

    pp_divisor_val_ = registers::PanelPowerDivisor::Get().ReadFrom(mmio_space_.get()).reg_value();
    pp_off_delay_val_ =
            registers::PanelPowerOffDelay::Get().ReadFrom(mmio_space_.get()).reg_value();
    pp_on_delay_val_  = registers::PanelPowerOnDelay::Get().ReadFrom(mmio_space_.get()).reg_value();
    sblc_ctrl2_val_ = registers::SouthBacklightCtl2::Get().ReadFrom(mmio_space_.get()).reg_value();
    schicken1_val_ = registers::SChicken1::Get().ReadFrom(mmio_space_.get()).reg_value();

    sblc_polarity_ = registers::SouthBacklightCtl1::Get().ReadFrom(mmio_space_.get()).polarity();
    ddi_a_lane_capability_control_ = registers::DdiRegs(registers::DDI_A).DdiBufControl()
            .ReadFrom(mmio_space_.get()).ddi_a_lane_capability_control();

    LOG_TRACE("Initialzing hotplug\n");
    status = interrupts_.Init(this);
    if (status != ZX_OK) {
        LOG_ERROR("Failed to init hotplugging\n");
        return status;
    }

    LOG_TRACE("Mapping gtt\n");
    {
        fbl::AutoLock lock(&gtt_lock_);
        if ((status = gtt_.Init(this)) != ZX_OK) {
            LOG_ERROR("Failed to init gtt (%d)\n", status);
            return status;
        }
    }

    status = DdkAdd("intel_i915");
    if (status != ZX_OK) {
        LOG_ERROR("Failed to add controller device\n");
        return status;
    }
    // DevMgr now owns this pointer, release it to avoid destroying the object
    // when device goes out of scope.
    __UNUSED auto ptr = controller_ptr->release();

    i915_gpu_core_device_proto.version = DEVICE_OPS_VERSION;
    i915_gpu_core_device_proto.release = gpu_release;
    // zx_gpu_dev_ is removed when unbind is called for zxdev() (in ::DdkUnbind),
    // so it's not necessary to give it its own unbind method.

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "intel-gpu-core";
    args.ctx = this;
    args.ops = &i915_gpu_core_device_proto;
    args.proto_id = ZX_PROTOCOL_INTEL_GPU_CORE;
    args.proto_ops = &i915_gpu_core_protocol_ops;
    status = device_add(zxdev(), &args, &zx_gpu_dev_);
    if (status != ZX_OK) {
        LOG_ERROR("Failed to publish gpu core device (%d)\n", status);
        device_remove(zxdev());
        return status;
    }

    LOG_TRACE("bind done\n");

    thrd_t init_thread;
    status = thrd_create_with_name(&init_thread, finish_init, this, "i915-init-thread");
    if (status != ZX_OK) {
        LOG_ERROR("Failed to create init thread\n");
        device_remove(zxdev());
        return status;
    }
    init_thrd_started_ = true;

    return ZX_OK;
}

Controller::Controller(zx_device_t* parent)
    : DeviceType(parent), power_(this) {
    mtx_init(&display_lock_, mtx_plain);
    mtx_init(&gtt_lock_, mtx_plain);
    mtx_init(&bar_lock_, mtx_plain);
    mtx_init(&_dc_cb_lock_, mtx_plain);
}

Controller::~Controller() {
    if (init_thrd_started_) {
        thrd_join(init_thread_, nullptr);
    }

    interrupts_.Destroy();
    if (mmio_space_) {
        EnableBacklight(false);
    }
    // Drop our own reference to bar 0. No-op if we failed before we mapped it.
    UnmapPciMmio(0u);
    // Release anything leaked by the gpu-core client.
    fbl::AutoLock lock(&bar_lock_);
    for (unsigned i = 0; i < PCI_MAX_BAR_COUNT; i++) {
        if (mapped_bars_[i].count) {
            LOG_INFO("Leaked bar %d\n", i);
            mapped_bars_[i].count = 1;
            UnmapPciMmio(i);
        }
    }
}

} // namespace i915

zx_status_t intel_i915_bind(void* ctx, zx_device_t* parent) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<i915::Controller> controller(new (&ac) i915::Controller(parent));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    return controller->Bind(&controller);
}
