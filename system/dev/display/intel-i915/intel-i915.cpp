// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/display.h>
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
#include <zx/vmar.h>
#include <zx/vmo.h>

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
static int irq_handler(void* arg) {
    return static_cast<i915::Controller*>(arg)->IrqLoop();
}

bool pipe_in_use(const fbl::Vector<i915::DisplayDevice*>& displays, registers::Pipe pipe) {
    for (size_t i = 0; i < displays.size(); i++) {
        if (displays[i]->pipe() == pipe) {
            return true;
        }
    }
    return false;
}
} // namespace

namespace i915 {

int Controller::IrqLoop() {
    for (;;) {
        uint64_t slots;
        if (zx_interrupt_wait(irq_, &slots) != ZX_OK) {
            zxlogf(TRACE, "i915: interrupt wait failed\n");
            break;
        }

        auto interrupt_ctrl = registers::MasterInterruptControl::Get().ReadFrom(mmio_space_.get());
        interrupt_ctrl.set_enable_mask(0);
        interrupt_ctrl.WriteTo(mmio_space_.get());

        if (interrupt_ctrl.sde_int_pending()) {
            auto sde_int_identity = registers::SdeInterruptBase::Get(registers::SdeInterruptBase::kSdeIntIdentity).ReadFrom(mmio_space_.get());
            auto hp_ctrl1 = registers::HotplugCtrl
                    ::Get(registers::DDI_A).ReadFrom(mmio_space_.get());
            auto hp_ctrl2 = registers::HotplugCtrl
                    ::Get(registers::DDI_E).ReadFrom(mmio_space_.get());
            for (uint32_t i = 0; i < registers::kDdiCount; i++) {
                registers::Ddi ddi = registers::kDdis[i];
                bool hp_detected = sde_int_identity.ddi_bit(ddi).get();
                auto hp_ctrl = ddi < registers::DDI_E ? hp_ctrl1 : hp_ctrl2;
                if (hp_detected && hp_ctrl.hpd_long_pulse(ddi).get()) {
                    HandleHotplug(ddi);
                }
            }
            // Write back the register to clear the bits
            hp_ctrl1.WriteTo(mmio_space_.get());
            hp_ctrl2.WriteTo(mmio_space_.get());
            sde_int_identity.WriteTo(mmio_space_.get());
        }

        interrupt_ctrl.set_enable_mask(1);
        interrupt_ctrl.WriteTo(mmio_space_.get());
    }
    return 0;
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

zx_status_t Controller::InitHotplug() {
    // Disable interrupts here, we'll re-enable them at the very end of ::Bind
    auto interrupt_ctrl = registers::MasterInterruptControl::Get().ReadFrom(mmio_space_.get());
    interrupt_ctrl.set_enable_mask(0);
    interrupt_ctrl.WriteTo(mmio_space_.get());

    uint32_t irq_cnt = 0;
    zx_status_t status = pci_query_irq_mode(&pci_, ZX_PCIE_IRQ_MODE_LEGACY, &irq_cnt);
    if (status != ZX_OK || !irq_cnt) {
        zxlogf(ERROR, "i915: Failed to find interrupts %d %d\n", status, irq_cnt);
        return ZX_ERR_INTERNAL;
    }

    if ((status = pci_set_irq_mode(&pci_, ZX_PCIE_IRQ_MODE_LEGACY, 1)) != ZX_OK) {
        zxlogf(ERROR, "i915: Failed to set irq mode %d\n", status);
        return status;
    }

    if ((status = pci_map_interrupt(&pci_, 0, &irq_) != ZX_OK)) {
        zxlogf(ERROR, "i915: Failed to map interrupt %d\n", status);
        return status;
    }

    status = thrd_create_with_name(&irq_thread_, irq_handler, this, "i915-irq-thread");
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: Failed to create irq thread\n");
        return status;
    }

    auto sfuse_strap = registers::SouthFuseStrap::Get().ReadFrom(mmio_space_.get());
    for (uint32_t i = 0; i < registers::kDdiCount; i++) {
        registers::Ddi ddi = registers::kDdis[i];
        bool enabled = (ddi == registers::DDI_A) || (ddi == registers::DDI_E) || (ddi == registers::DDI_B && sfuse_strap.port_b_present()) || (ddi == registers::DDI_C && sfuse_strap.port_c_present()) || (ddi == registers::DDI_D && sfuse_strap.port_d_present());

        auto hp_ctrl = registers::HotplugCtrl::Get(ddi).ReadFrom(mmio_space_.get());
        hp_ctrl.hpd_enable(ddi).set(enabled);
        hp_ctrl.WriteTo(mmio_space_.get());

        auto mask = registers::SdeInterruptBase::Get(
                        registers::SdeInterruptBase::kSdeIntMask)
                        .ReadFrom(mmio_space_.get());
        mask.ddi_bit(ddi).set(!enabled);
        mask.WriteTo(mmio_space_.get());

        auto enable = registers::SdeInterruptBase::Get(
                          registers::SdeInterruptBase::kSdeIntEnable)
                          .ReadFrom(mmio_space_.get());
        enable.ddi_bit(ddi).set(enabled);
        enable.WriteTo(mmio_space_.get());
    }

    return ZX_OK;
}

void Controller::HandleHotplug(registers::Ddi ddi) {
    zxlogf(TRACE, "i915: hotplug detected %d\n", ddi);
    DisplayDevice* device = nullptr;
    bool was_kernel_framebuffer = false;
    for (size_t i = 0; i < display_devices_.size(); i++) {
        if (display_devices_[i]->ddi() == ddi) {
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
    } else { // New device was plugged in
        fbl::unique_ptr<DisplayDevice> device = InitDisplay(ddi);
        if (!device) {
            zxlogf(INFO, "i915: failed to init hotplug display\n");
            return;
        }

        if (AddDisplay(fbl::move(device)) != ZX_OK) {
            zxlogf(INFO, "Failed to add display %d\n", ddi);
        }
    }
}

bool Controller::BringUpDisplayEngine() {
    // Enable PCH Reset Handshake
    auto nde_rstwrn_opt = registers::NorthDERestetWarning::Get().ReadFrom(mmio_space_.get());
    nde_rstwrn_opt.set_rst_pch_handshake_enable(1);
    nde_rstwrn_opt.WriteTo(mmio_space_.get());

    // Wait for Power Well 0 distribution
    if (!WAIT_ON_US(registers::FuseStatus::Get().ReadFrom(mmio_space_.get()).pg0_dist_status(), 5)) {
        zxlogf(ERROR, "Power Well 0 distribution failed\n");
        return false;
    }

    // Enable and wait for Power Well 1 and Misc IO power
    auto power_well = registers::PowerWellControl2::Get().ReadFrom(mmio_space_.get());
    power_well.set_power_well_1_request(1);
    power_well.set_misc_io_power_state(1);
    power_well.WriteTo(mmio_space_.get());
    if (!WAIT_ON_US(registers::PowerWellControl2::Get().ReadFrom(mmio_space_.get()).power_well_1_state(), 10)) {
        zxlogf(ERROR, "Power Well 1 failed to enable\n");
        return false;
    }
    if (!WAIT_ON_US(registers::PowerWellControl2::Get().ReadFrom(mmio_space_.get()).misc_io_power_state(), 10)) {
        zxlogf(ERROR, "Misc IO power failed to enable\n");
        return false;
    }
    if (!WAIT_ON_US(registers::FuseStatus::Get().ReadFrom(mmio_space_.get()).pg1_dist_status(), 5)) {
        zxlogf(ERROR, "Power Well 1 distribution failed\n");
        return false;
    }

    // Enable CDCLK PLL to 337.5mhz if the BIOS didn't already enable it. If it needs to be
    // something special (i.e. for eDP), assume that the BIOS already enabled it.
    auto dpll_enable = registers::DpllEnable::Get(0).ReadFrom(mmio_space_.get());
    if (!dpll_enable.enable_dpll()) {
        // Set the cd_clk frequency to the minimum
        auto cd_clk = registers::CdClockCtl::Get().ReadFrom(mmio_space_.get());
        cd_clk.set_cd_freq_select(cd_clk.kFreqSelect3XX);
        cd_clk.set_cd_freq_decimal(cd_clk.kFreqDecimal3375);
        cd_clk.WriteTo(mmio_space_.get());

        // Configure DPLL0
        auto dpll_ctl1 = registers::DpllControl1::Get().ReadFrom(mmio_space_.get());
        dpll_ctl1.dpll_link_rate(0).set(dpll_ctl1.kLinkRate810Mhz);
        dpll_ctl1.dpll_override(0).set(1);
        dpll_ctl1.dpll_hdmi_mode(0).set(0);
        dpll_ctl1.dpll_ssc_enable(0).set(0);
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

    return true;
}

bool Controller::ResetPipe(registers::Pipe pipe) {
    registers::PipeRegs pipe_regs(pipe);
    registers::TranscoderRegs trans_regs(pipe);

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

    auto trans_clk_sel = trans_regs.ClockSelect().ReadFrom(mmio_space());
    trans_clk_sel.set_trans_clock_select(0);
    trans_clk_sel.WriteTo(mmio_space());

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
    dpll_ctrl2.ddi_clock_off(ddi).set(1);
    dpll_ctrl2.WriteTo(mmio_space());

    uint8_t dpll_number = static_cast<uint8_t>(dpll_ctrl2.ddi_clock_select(ddi).get());
    auto dpll_enable = registers::DpllEnable::Get(dpll_number).ReadFrom(mmio_space());
    dpll_enable.set_enable_dpll(1);
    dpll_enable.WriteTo(mmio_space());

    return true;
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
    } else if (!pipe_in_use(display_devices_, registers::PIPE_B)
            && !registers::HdportState::Get().ReadFrom(mmio_space_.get()).dpll2_used()) {
        pipe = registers::PIPE_B;
    } else if (!pipe_in_use(display_devices_, registers::PIPE_C)) {
        pipe = registers::PIPE_C;
    } else {
        zxlogf(INFO, "i915: Could not allocate pipe for ddi %d\n", ddi);
        return nullptr;
    }

    fbl::AllocChecker ac;
    if (igd_opregion_.IsHdmi(ddi) || igd_opregion_.IsDvi(ddi)) {
        zxlogf(SPEW, "Checking for hdmi monitor\n");
        auto hdmi_disp = fbl::make_unique_checked<HdmiDisplay>(&ac, this, ddi, pipe);
        if (ac.check() && reinterpret_cast<DisplayDevice*>(hdmi_disp.get())->Init()) {
            return hdmi_disp;
        }
    } else if (igd_opregion_.IsDp(ddi)) {
        zxlogf(SPEW, "Checking for displayport monitor\n");
        auto dp_disp = fbl::make_unique_checked<DpDisplay>(&ac, this, ddi, pipe);
        if (ac.check() && reinterpret_cast<DisplayDevice*>(dp_disp.get())->Init()) {
            return dp_disp;
        }
    } else {
        zxlogf(SPEW, "Skipping ddi\n");
    }

    return nullptr;
}

zx_status_t Controller::InitDisplays() {
    if (ENABLE_MODESETTING && is_gen9(device_id_)) {
        BringUpDisplayEngine();

        for (unsigned i = 0; i < registers::kPipeCount; i++) {
            ResetPipe(registers::kPipes[i]);
        }

        for (unsigned i = 0; i < registers::kDdiCount; i++) {
            ResetDdi(registers::kDdis[i]);
        }

        AllocDisplayBuffers();

        for (uint32_t i = 0; i < registers::kDdiCount; i++) {
            auto disp_device = InitDisplay(registers::kDdis[i]);
            if (disp_device) {
                if (AddDisplay(fbl::move(disp_device)) != ZX_OK) {
                    zxlogf(INFO, "Failed to add display %d\n", i);
                }
            }
        }
        return ZX_OK;
    } else {
        fbl::AllocChecker ac;
        // The DDI doesn't actually matter, so just say DDI A. The BIOS does use PIPE_A.
        auto disp_device = fbl::make_unique_checked<BootloaderDisplay>(
                &ac, this, registers::DDI_A, registers::PIPE_A);
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
    return ZX_OK;
}

void Controller::DdkUnbind() {
    while (!display_devices_.is_empty()) {
        device_remove(display_devices_.erase(0)->zxdev());
    }
    device_remove(zxdev());
}

void Controller::DdkRelease() {
    delete this;
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

        gtt_.SetupForMexec(fb, fb_size, registers::PlaneSurface::kTrailingPtePadding);

        // Try to map the framebuffer and clear it. If not, oh well.
        void* gmadr;
        uint64_t gmadr_size;
        zx_handle_t gmadr_handle;
        if (pci_map_bar(&pci_, 2, ZX_CACHE_POLICY_WRITE_COMBINING,
                        &gmadr, &gmadr_size, &gmadr_handle) == ZX_OK) {
            memset(reinterpret_cast<void*>(gmadr), 0, fb_size);
            zx_handle_close(gmadr_handle);
        }

        for (auto* display : display_devices_) {
            // TODO(ZX-1413): Reset/scale the display to ensure the buffer displays properly
            registers::PipeRegs pipe_regs(display->pipe());

            auto plane_stride = pipe_regs.PlaneSurfaceStride().ReadFrom(mmio_space_.get());
            plane_stride.set_stride(stride / registers::PlaneSurfaceStride::kLinearStrideChunkSize);
            plane_stride.WriteTo(mmio_space_.get());

            auto plane_surface = pipe_regs.PlaneSurface().ReadFrom(mmio_space_.get());
            plane_surface.set_surface_base_addr(0);
            plane_surface.WriteTo(mmio_space_.get());
        }
    }
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
    if (is_gen9(device_id_) && ENABLE_MODESETTING) {
        status = igd_opregion_.Init(&pci_);
        if (status != ZX_OK) {
            zxlogf(ERROR, "i915: Failed to init VBT (%d)\n", status);
            return status;
        }
    }

    zxlogf(TRACE, "i915: mapping registers\n");
    // map register window
    uintptr_t regs;
    uint64_t regs_size;
    status = pci_map_bar(&pci_, 0u, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                              reinterpret_cast<void**>(&regs), &regs_size, &regs_handle_);
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

    if (ENABLE_MODESETTING && is_gen9(device_id_)) {
        zxlogf(TRACE, "i915: initialzing hotplug\n");
        status = InitHotplug();
        if (status != ZX_OK) {
            zxlogf(ERROR, "i915: failed to init hotplugging\n");
            return status;
        }
    }

    zxlogf(TRACE, "i915: mapping gtt\n");
    if ((status = gtt_.Init(this)) != ZX_OK) {
        zxlogf(ERROR, "i915: failed to init gtt %d\n", status);
        return status;
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

    if (is_gen9(device_id_)) {
        auto interrupt_ctrl = registers::MasterInterruptControl::Get().ReadFrom(mmio_space_.get());
        interrupt_ctrl.set_enable_mask(1);
        interrupt_ctrl.WriteTo(mmio_space_.get());
    }

    // TODO remove when the gfxconsole moves to user space
    EnableBacklight(true);

    zxlogf(TRACE, "i915: initialization done\n");

    return ZX_OK;
}

Controller::Controller(zx_device_t* parent)
    : DeviceType(parent), irq_(ZX_HANDLE_INVALID) {}

Controller::~Controller() {
    if (irq_ != ZX_HANDLE_INVALID) {
        zx_interrupt_signal(irq_, ZX_INTERRUPT_SLOT_USER, 0);

        thrd_join(irq_thread_, nullptr);

        zx_handle_close(irq_);
    }

    if (mmio_space_) {
        EnableBacklight(false);

        zx_handle_close(regs_handle_);
        regs_handle_ = ZX_HANDLE_INVALID;
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
