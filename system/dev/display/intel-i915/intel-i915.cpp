// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/intel-gpu-core.h>
#include <ddk/protocol/pci.h>
#include <hw/inout.h>
#include <hw/pci.h>

#include <assert.h>
#include <fbl/unique_ptr.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include "bootloader-display.h"
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

#define INTEL_I915_BROADWELL_DID (0x1616)

#define INTEL_I915_REG_WINDOW_SIZE (0x1000000u)
#define INTEL_I915_FB_WINDOW_SIZE (0x10000000u)

#define BACKLIGHT_CTRL_OFFSET (0xc8250)
#define BACKLIGHT_CTRL_BIT ((uint32_t)(1u << 31))

#define FLAGS_BACKLIGHT 1

#define ENABLE_MODESETTING 1

namespace {
static const zx_pixel_format_t supported_formats[1] = { ZX_PIXEL_FORMAT_ARGB_8888 };

bool pipe_in_use(const fbl::Vector<i915::DisplayDevice*>& displays, registers::Pipe pipe) {
    for (size_t i = 0; i < displays.size(); i++) {
        if (displays[i]->pipe() == pipe) {
            return true;
        }
    }
    return false;
}

static inline bool is_modesetting_enabled(uint16_t device_id) {
    return ENABLE_MODESETTING && i915::is_gen9(device_id);
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

} // namespace

namespace i915 {

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
    zxlogf(TRACE, "i915: hotplug detected %d %d\n", ddi, long_pulse);
    DisplayDevice* device = nullptr;
    bool was_kernel_framebuffer = false;
    int32_t display_added = -1;
    int32_t display_removed = -1;

    acquire_dc_cb_lock();
    {
        fbl::AutoLock lock(&display_lock_);

        for (size_t i = 0; i < display_devices_.size(); i++) {
            if (display_devices_[i]->ddi() == ddi) {
                if (display_devices_[i]->HandleHotplug(long_pulse)) {
                    zxlogf(SPEW, "i915: hotplug handled by device\n");
                    release_dc_cb_lock();
                    return;
                }
                device = display_devices_.erase(i);
                was_kernel_framebuffer = i == 0;
                break;
            }
        }
        if (device) { // Existing device was unplugged
            if (was_kernel_framebuffer) {
                if (display_devices_.is_empty()) {
                    zx_set_framebuffer_vmo(get_root_resource(), ZX_HANDLE_INVALID, 0, 0, 0, 0, 0);
                } else {
                    DisplayDevice* new_device = display_devices_[0];
                    zx_set_framebuffer_vmo(get_root_resource(),
                                           new_device->framebuffer_vmo().get(),
                                           static_cast<uint32_t>(new_device->framebuffer_size()),
                                           new_device->info().format, new_device->info().width,
                                           new_device->info().height, new_device->info().stride);
                }
            }
            device->DdkRemove();
            zxlogf(SPEW, "Display unplugged\n");
            display_removed = device->id();
        } else { // New device was plugged in
            fbl::unique_ptr<DisplayDevice> device = InitDisplay(ddi);
            if (!device) {
                zxlogf(INFO, "i915: failed to init hotplug display\n");
            } else {
                int id = device->id();
                if (AddDisplay(fbl::move(device)) == ZX_OK) {
                    zxlogf(SPEW, "Display connected\n");
                    display_added = id;
                } else {
                    zxlogf(INFO, "Failed to add display %d\n", ddi);
                }
            }
        }
    }
    if (dc_cb() && (display_added >= 0 || display_removed >= 0)) {
        dc_cb()->on_displays_changed(dc_cb_ctx_,
                                     &display_added, display_added >= 0,
                                     &display_removed, display_removed >= 0);
    }
    release_dc_cb_lock();
}

void Controller::HandlePipeVsync(registers::Pipe pipe) {
    acquire_dc_cb_lock();

    if (!dc_cb()) {
        release_dc_cb_lock();
        return;
    }

    int32_t id = -1;
    void* handle = nullptr;
    {
        fbl::AutoLock lock(&display_lock_);
        for (auto display : display_devices_) {
            if (display->pipe() == pipe) {
                registers::PipeRegs regs(pipe);
                auto live_surface = regs.PlaneSurfaceLive().ReadFrom(mmio_space());
                handle = reinterpret_cast<void*>(
                        live_surface.surface_base_addr() << live_surface.kPageShift);
                id = display->id();
                break;
            }
        }
    }

    if (id >= 0) {
        dc_cb()->on_display_vsync(dc_cb_ctx_, id, handle);
    }
    release_dc_cb_lock();
}

DisplayDevice* Controller::FindDevice(int32_t display_id) {
    for (auto d : display_devices_) {
        if (d->id() == display_id) {
            return d;
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
        zxlogf(ERROR, "Power Well 0 distribution failed\n");
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
            zxlogf(ERROR, "Failed to configure dpll0\n");
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
                zxlogf(ERROR, "GT Driver Mailbox driver busy\n");
                return false;
            }
            if (mmio_space_.get()->Read<uint32_t>(kGtDriverMailboxData0) & 0x1) {
                break;
            }
            if (count++ == 3) {
                zxlogf(ERROR, "Failed to set cd_clk\n");
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
        zxlogf(ERROR, "Failed to enable DBUF\n");
        return false;
    }

    // We never use VGA, so just disable it at startup
    constexpr uint16_t kSequencerIdx = 0x3c4;
    constexpr uint16_t kSequencerData = 0x3c5;
    constexpr uint8_t kClockingModeIdx = 1;
    constexpr uint8_t kClockingModeScreenOff = (1 << 5);
    zx_status_t status = zx_mmap_device_io(get_root_resource(), kSequencerIdx, 2);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to map vga ports\n");
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

    AllocDisplayBuffers();

    return true;
}

void Controller::ResetPipe(registers::Pipe pipe) {
    registers::PipeRegs pipe_regs(pipe);

    // Disable planes
    pipe_regs.PlaneControl().FromValue(0).WriteTo(mmio_space());
    pipe_regs.PlaneSurface().FromValue(0).WriteTo(mmio_space());

    // Disable the scalers (double buffered on PipeScalerWinSize)
    pipe_regs.PipeScalerCtrl(0).ReadFrom(mmio_space()).set_enable(0).WriteTo(mmio_space());
    pipe_regs.PipeScalerWinSize(0).ReadFrom(mmio_space()).WriteTo(mmio_space());
    if (pipe != registers::PIPE_C) {
        pipe_regs.PipeScalerCtrl(1).ReadFrom(mmio_space()).set_enable(0).WriteTo(mmio_space());
        pipe_regs.PipeScalerWinSize(1).ReadFrom(mmio_space()).WriteTo(mmio_space());
    }
}

bool Controller::ResetTrans(registers::Trans trans) {
    registers::TranscoderRegs trans_regs(trans);

    // Disable transcoder and wait it to stop
    auto trans_conf = trans_regs.Conf().ReadFrom(mmio_space());
    trans_conf.set_transcoder_enable(0);
    trans_conf.WriteTo(mmio_space());
    if (!WAIT_ON_MS(!trans_regs.Conf().ReadFrom(mmio_space()).transcoder_state(), 60)) {
        zxlogf(ERROR, "Failed to reset transcoder\n");
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
        zxlogf(ERROR, "Port failed to go idle\n");
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
        zxlogf(SPEW, "Selected DPLL %d\n", res);
    } else {
        zxlogf(INFO, "Failed to allocate DPLL\n");
    }

    return res;
}

void Controller::AllocDisplayBuffers() {
    // Do display buffer alloc and watermark programming with fixed allocation from
    // intel docs. This allows the display to work but prevents power management.
    // TODO(ZX-1413): Calculate these dynamically based on what's enabled.
    for (unsigned i = 0; i < registers::kPipeCount; i++) {
        registers::Pipe pipe = registers::kPipes[i];
        registers::PipeRegs pipe_regs(pipe);

        // Plane 1 gets everything
        constexpr uint32_t kPerDdi = 891 / 3;
        auto buf_cfg = pipe_regs.PlaneBufCfg(1).FromValue(0);
        buf_cfg.set_buffer_start(kPerDdi * pipe);
        buf_cfg.set_buffer_end(kPerDdi * (pipe + 1) - 1);
        buf_cfg.WriteTo(mmio_space());

        // Cursor and planes 2 and 3 get nothing
        pipe_regs.PlaneBufCfg(0).FromValue(0).WriteTo(mmio_space());
        pipe_regs.PlaneBufCfg(2).FromValue(0).WriteTo(mmio_space());
        pipe_regs.PlaneBufCfg(3).FromValue(0).WriteTo(mmio_space());

        auto wm0 = pipe_regs.PlaneWatermark(0).FromValue(0);
        wm0.set_enable(1);
        wm0.set_lines(2);
        wm0.set_blocks(kPerDdi);
        wm0.WriteTo(mmio_space());

        for (int i = 1; i < 8; i++) {
            auto wm = pipe_regs.PlaneWatermark(i).FromValue(0);
            wm.WriteTo(mmio_space());
        }

        // Write so double-buffered regs are updated
        auto base = pipe_regs.PlaneSurface().ReadFrom(mmio_space());
        base.WriteTo(mmio_space());
    }
    // TODO(ZX-1413): Wait for vblank instead of sleeping
    zx_nanosleep(zx_deadline_after(ZX_MSEC(33)));
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
        zxlogf(INFO, "i915: Could not allocate pipe for ddi %d\n", ddi);
        return nullptr;
    }
    // It'd be possible to handle this by looking for an id which isn't currently in use. But
    // a lot of clients probably assume that display ids are completely unique, so just fail. It's
    // unlikely that we'd ever run into a system with >2 billion hotplug events.
    if (next_id_ < 0) {
        return nullptr;
    }

    fbl::AllocChecker ac;
    if (igd_opregion_.SupportsDp(ddi)) {
        zxlogf(SPEW, "Checking for displayport monitor\n");
        auto dp_disp = fbl::make_unique_checked<DpDisplay>(&ac, this, next_id_, ddi, pipe);
        if (ac.check() && reinterpret_cast<DisplayDevice*>(dp_disp.get())->Init()) {
            return dp_disp;
        }
    }
    if (igd_opregion_.SupportsHdmi(ddi) || igd_opregion_.SupportsDvi(ddi)) {
        zxlogf(SPEW, "Checking for hdmi monitor\n");
        auto hdmi_disp = fbl::make_unique_checked<HdmiDisplay>(&ac, this, next_id_, ddi, pipe);
        if (ac.check() && reinterpret_cast<DisplayDevice*>(hdmi_disp.get())->Init()) {
            return hdmi_disp;
        }
    }

    return nullptr;
}

zx_status_t Controller::InitDisplays() {
    if (is_modesetting_enabled(device_id_)) {
        BringUpDisplayEngine(false);

        acquire_dc_cb_lock();
        int displays[registers::kDdiCount];
        uint32_t display_count;
        {
            fbl::AutoLock lock(&display_lock_);
            for (uint32_t i = 0; i < registers::kDdiCount; i++) {
                auto disp_device = InitDisplay(registers::kDdis[i]);
                if (disp_device) {
                    if (AddDisplay(fbl::move(disp_device)) != ZX_OK) {
                        zxlogf(INFO, "Failed to add display %d\n", i);
                    }
                }
            }
            display_count = static_cast<uint32_t>(display_devices_.size());
            for (unsigned i = 0; i < display_count; i++) {
                displays[i] = display_devices_[i]->id();
            }
        }

        // TODO(stevensd): Once displays are no longer real ddk devices, move
        // InitDisplays before DdkAdd so that dc_cb_ can't be set before here. Also
        // remove the requirement for dc_cb_lock()
        if (display_count && dc_cb()) {
            dc_cb()->on_displays_changed(dc_cb_ctx_, displays, display_count, nullptr, 0);
        }
        release_dc_cb_lock();

        return ZX_OK;
    } else {
        fbl::AutoLock lock(&display_lock_);

        fbl::AllocChecker ac;
        // The DDI doesn't actually matter, so just say DDI A. The BIOS does use PIPE_A.
        auto disp_device = fbl::make_unique_checked<BootloaderDisplay>(
                &ac, this, next_id_, registers::DDI_A, registers::PIPE_A);
        if (!ac.check()) {
            zxlogf(ERROR, "i915: failed to alloc disp_device\n");
            return ZX_ERR_NO_MEMORY;
        }

        if (!reinterpret_cast<DisplayDevice*>(disp_device.get())->Init()) {
            zxlogf(ERROR, "i915: failed to init display\n");
            return ZX_ERR_INTERNAL;
        }
        return AddDisplay(fbl::move(disp_device));
    }
}

zx_status_t Controller::AddDisplay(fbl::unique_ptr<DisplayDevice>&& display) {
    zx_status_t status = display->DdkAdd("intel_i915_disp");
    fbl::AllocChecker ac;
    display_devices_.reserve(display_devices_.size() + 1, &ac);

    if (ac.check() && status == ZX_OK) {
        display_devices_.push_back(display.release(), &ac);
        assert(ac.check());
    } else {
        zxlogf(ERROR, "i915: failed to add display device %d\n", status);
        return status == ZX_OK ? ZX_ERR_NO_MEMORY : status;
    }

    if (display_devices_.size() == 1) {
        DisplayDevice* new_device = display_devices_[0];
        zx_set_framebuffer_vmo(get_root_resource(), new_device->framebuffer_vmo().get(),
                               static_cast<uint32_t>(new_device->framebuffer_size()),
                               new_device->info().format, new_device->info().width,
                               new_device->info().height, new_device->info().stride);
    }

    next_id_++;
    return ZX_OK;
}

// DisplayController methods

void Controller::SetDisplayControllerCb(void* cb_ctx, display_controller_cb_t* cb) {
    acquire_dc_cb_lock();
    dc_cb_ctx_ = cb_ctx;
    _dc_cb_ = cb;

    int displays[registers::kDdiCount];
    uint32_t size;
    {
        fbl::AutoLock lock(&display_lock_);
        size = static_cast<uint32_t>(display_devices_.size());
        for (unsigned i = 0; i < size; i++) {
            displays[i] = display_devices_[i]->id();
        }
    }

    cb->on_displays_changed(cb_ctx, displays, size, NULL, 0);
    release_dc_cb_lock();
}

zx_status_t Controller::GetDisplayInfo(int32_t display_id, display_info_t* info) {
    DisplayDevice* device;
    fbl::AutoLock lock(&display_lock_);
    if ((device = FindDevice(display_id)) == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }
    info->edid = device->edid().edid_bytes();
    info->edid_length = device->edid().edid_length();
    info->pixel_formats = supported_formats;
    info->pixel_format_count = static_cast<uint32_t>(fbl::count_of(supported_formats));
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

    uint32_t length = image->height * ZX_PIXEL_FORMAT_BYTES(image->pixel_format) *
            registers::PlaneSurfaceStride::compute_pixel_stride(image->type, image->width,
                                                                image->pixel_format);
    fbl::unique_ptr<GttRegion> gtt_region;
    zx_status_t status = gtt_.AllocRegion(length,
                                          registers::PlaneSurface::kLinearAlignment,
                                          registers::PlaneSurface::kTrailingPtePadding,
                                          &gtt_region);
    if (status != ZX_OK) {
        return status;
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

bool Controller::CheckConfiguration(display_config_t** display_config, uint32_t display_count) {
    fbl::AutoLock lock(&display_lock_);
    for (unsigned i = 0; i < display_count; i++) {
        auto* config = display_config[i];
        if (!FindDevice(config->display_id)) {
            return false;
        }
        if (config->image.width != config->h_active || config->image.height != config->v_active
                || config->image.pixel_format != ZX_PIXEL_FORMAT_ARGB_8888) {
            return false;
        }
    }
    return true;
}

void Controller::ApplyConfiguration(display_config_t** display_config, uint32_t display_count) {
    ZX_DEBUG_ASSERT(CheckConfiguration(display_config, display_count));
    fbl::AutoLock lock(&display_lock_);

    for (auto display : display_devices_) {
        display_config_t* config = nullptr;
        for (unsigned i = 0; i < display_count; i++) {
            if (display_config[i]->display_id == display->id()) {
                config = display_config[i];
                break;
            }
        }
        if (config != nullptr) {
            display->ApplyConfiguration(config);
        }
    }
}

uint32_t Controller::ComputeLinearStride(uint32_t width, zx_pixel_format_t format) {
    return registers::PlaneSurfaceStride::compute_pixel_stride(IMAGE_TYPE_SIMPLE, width, format);
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
                                          PAGE_SIZE, 0, &region);
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
    fbl::AutoLock lock(&display_lock_);
    while (!display_devices_.is_empty()) {
        device_remove(display_devices_.erase(0)->zxdev());
    }
    device_remove(zxdev());
    device_remove(zx_gpu_dev_);
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
        if (zx_bootloader_fb_get_info(&format, &width, &height, &stride) != ZX_OK) {
            return ZX_OK;
        }

        // The bootloader framebuffer is most likely at the start of the display
        // controller's bar 2. Try to get that buffer working again across the
        // mexec by mapping gfx stolen memory to gaddr 0.

        auto bdsm_reg = registers::BaseDsm::Get().FromValue(0);
        zx_status_t status =
                pci_config_read32(&pci_, bdsm_reg.kAddr, bdsm_reg.reg_value_ptr());
        if (status != ZX_OK) {
            zxlogf(TRACE, "i915: failed to read dsm base\n");
            return ZX_OK;
        }

        // The Intel docs say that the first page should be reserved for the gfx
        // hardware, but a lot of BIOSes seem to ignore that.
        uintptr_t fb = bdsm_reg.base_phys_addr() << bdsm_reg.base_phys_addr_shift;
        uint32_t fb_size = stride * height * ZX_PIXEL_FORMAT_BYTES(format);

        {
            fbl::AutoLock lock(&gtt_lock_);
            gtt_.SetupForMexec(fb, fb_size, registers::PlaneSurface::kTrailingPtePadding);
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
            for (auto* display : display_devices_) {
                // TODO(ZX-1413): Reset/scale the display to ensure the buffer displays properly
                registers::PipeRegs pipe_regs(display->pipe());

                auto plane_stride = pipe_regs.PlaneSurfaceStride().ReadFrom(mmio_space_.get());
                plane_stride.set_stride(IMAGE_TYPE_SIMPLE, stride, format);
                plane_stride.WriteTo(mmio_space_.get());

                auto plane_surface = pipe_regs.PlaneSurface().ReadFrom(mmio_space_.get());
                plane_surface.set_surface_base_addr(0);
                plane_surface.WriteTo(mmio_space_.get());
            }
        }
    }
    return ZX_OK;
}

zx_status_t Controller::DdkResume(uint32_t hint) {
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

    fbl::AutoLock lock(&display_lock_);
    for (auto& disp : display_devices_) {
        if (!disp->Resume()) {
            zxlogf(ERROR, "Failed to resume display\n");
        }
    }

    interrupts_.Resume();

    return ZX_OK;
}

zx_status_t Controller::Bind(fbl::unique_ptr<i915::Controller>* controller_ptr) {
    zxlogf(TRACE, "i915: binding to display controller\n");

    if (device_get_protocol(parent_, ZX_PROTOCOL_PCI, &pci_)) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    pci_config_read16(&pci_, PCI_CONFIG_DEVICE_ID, &device_id_);
    zxlogf(TRACE, "i915: device id %x\n", device_id_);
    if (device_id_ == INTEL_I915_BROADWELL_DID) {
        // TODO: this should be based on the specific target
        flags_ |= FLAGS_BACKLIGHT;
    }

    zx_status_t status;
    if (is_modesetting_enabled(device_id_)) {
        status = igd_opregion_.Init(&pci_);
        if (status != ZX_OK) {
            zxlogf(ERROR, "i915: Failed to init VBT (%d)\n", status);
            return status;
        }
    }

    zxlogf(TRACE, "i915: mapping registers\n");
    // map register window
    void* regs;
    uint64_t size;
    status = MapPciMmio(0u, &regs, &size);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: failed to map bar 0: %d\n", status);
        return status;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<hwreg::RegisterIo> mmio_space(
            new (&ac) hwreg::RegisterIo(reinterpret_cast<volatile void*>(regs)));
    if (!ac.check()) {
        zxlogf(ERROR, "i915: failed to alloc RegisterIo\n");
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

    if (is_modesetting_enabled(device_id_)) {
        zxlogf(TRACE, "i915: initialzing hotplug\n");
        status = interrupts_.Init(this);
        if (status != ZX_OK) {
            zxlogf(ERROR, "i915: failed to init hotplugging\n");
            return status;
        }
    }

    zxlogf(TRACE, "i915: mapping gtt\n");
    {
        fbl::AutoLock lock(&gtt_lock_);
        if ((status = gtt_.Init(this)) != ZX_OK) {
            zxlogf(ERROR, "i915: failed to init gtt %d\n", status);
            return status;
        }
    }

    status = DdkAdd("intel_i915");
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: failed to add controller device\n");
        return status;
    }
    // DevMgr now owns this pointer, release it to avoid destroying the object
    // when device goes out of scope.
    __UNUSED auto ptr = controller_ptr->release();

    zxlogf(TRACE, "i915: initializing displays\n");
    status = InitDisplays();
    if (status != ZX_OK) {
        device_remove(zxdev());
        return status;
    }

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
        zxlogf(ERROR, "i915: Failed to publish gpu core device %d\n", status);
        device_remove(zxdev());
        return status;
    }

    if (is_modesetting_enabled(device_id_)) {
        interrupts_.FinishInit();
    }

    // TODO remove when the gfxconsole moves to user space
    EnableBacklight(true);

    zxlogf(TRACE, "i915: initialization done\n");

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
            zxlogf(INFO, "Leaked bar %d\n", i);
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
