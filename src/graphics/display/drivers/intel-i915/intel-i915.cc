// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-i915.h"

#include <assert.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <inttypes.h>
#include <lib/device-protocol/pci.h>
#include <lib/image-format/image_format.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/intelgpucore.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/sysmem.h>
#include <fbl/vector.h>
#include <hw/inout.h>

#include "dp-display.h"
#include "hdmi-display.h"
#include "macros.h"
#include "pci-ids.h"
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
static zx_pixel_format_t supported_formats[4] = {
    ZX_PIXEL_FORMAT_ARGB_8888, ZX_PIXEL_FORMAT_RGB_x888, ZX_PIXEL_FORMAT_ABGR_8888,
    ZX_PIXEL_FORMAT_BGR_888x};

static cursor_info_t cursor_infos[3] = {
    {.width = 64, .height = 64, .format = ZX_PIXEL_FORMAT_ARGB_8888},
    {.width = 128, .height = 128, .format = ZX_PIXEL_FORMAT_ARGB_8888},
    {.width = 256, .height = 256, .format = ZX_PIXEL_FORMAT_ARGB_8888},
};
static uint32_t image_types[4] = {
    IMAGE_TYPE_SIMPLE,
    IMAGE_TYPE_X_TILED,
    IMAGE_TYPE_Y_LEGACY_TILED,
    IMAGE_TYPE_YF_TILED,
};

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
                                               const zx_intel_gpu_core_interrupt_t* callback,
                                               uint32_t interrupt_mask) {
  return static_cast<i915::Controller*>(ctx)->RegisterInterruptCallback(callback, interrupt_mask);
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
    .read_pci_config16 = read_pci_config_16,
    .map_pci_mmio = map_pci_mmio,
    .unmap_pci_mmio = unmap_pci_mmio,
    .get_pci_bti = get_pci_bti,
    .register_interrupt_callback = register_interrupt_callback,
    .unregister_interrupt_callback = unregister_interrupt_callback,
    .gtt_get_size = gtt_get_size,
    .gtt_alloc = gtt_alloc,
    .gtt_free = gtt_free,
    .gtt_clear = gtt_clear,
    .gtt_insert = gtt_insert};

static void gpu_release(void* ctx) { static_cast<i915::Controller*>(ctx)->GpuRelease(); }

static zx_protocol_device_t i915_gpu_core_device_proto = {};

static uint32_t get_bus_count(void* ctx) {
  return static_cast<i915::Controller*>(ctx)->GetBusCount();
}

static zx_status_t get_max_transfer_size(void* ctx, uint32_t bus_id, size_t* out_size) {
  return static_cast<i915::Controller*>(ctx)->GetMaxTransferSize(bus_id, out_size);
}

static zx_status_t set_bitrate(void* ctx, uint32_t bus_id, uint32_t bitrate) {
  return static_cast<i915::Controller*>(ctx)->SetBitrate(bus_id, bitrate);
}

static zx_status_t transact(void* ctx, uint32_t bus_id, const i2c_impl_op_t* ops, size_t count) {
  return static_cast<i915::Controller*>(ctx)->Transact(bus_id, ops, count);
}

static i2c_impl_protocol_ops_t i2c_ops = {
    .get_bus_count = get_bus_count,
    .get_max_transfer_size = get_max_transfer_size,
    .set_bitrate = set_bitrate,
    .transact = transact,
};

static int finish_init(void* arg) {
  static_cast<i915::Controller*>(arg)->FinishInit();
  return 0;
}

const display_config_t* find_config(uint64_t display_id, const display_config_t** display_configs,
                                    size_t display_count) {
  for (size_t i = 0; i < display_count; i++) {
    if (display_configs[i]->display_id == display_id) {
      return display_configs[i];
    }
  }
  return nullptr;
}

static void get_posttransform_width(const layer_t& layer, uint32_t* width, uint32_t* height) {
  const primary_layer_t* primary = &layer.cfg.primary;
  if (primary->transform_mode == FRAME_TRANSFORM_IDENTITY ||
      primary->transform_mode == FRAME_TRANSFORM_ROT_180 ||
      primary->transform_mode == FRAME_TRANSFORM_REFLECT_X ||
      primary->transform_mode == FRAME_TRANSFORM_REFLECT_Y) {
    *width = primary->src_frame.width;
    *height = primary->src_frame.height;
  } else {
    *width = primary->src_frame.height;
    *height = primary->src_frame.width;
  }
}

}  // namespace

namespace i915 {

bool Controller::CompareDpllStates(const dpll_state_t& a, const dpll_state_t& b) {
  if (a.is_hdmi != b.is_hdmi) {
    return false;
  } else if (a.is_hdmi) {
    return a.hdmi.dco_int == b.hdmi.dco_int && a.hdmi.dco_frac == b.hdmi.dco_frac &&
           a.hdmi.q == b.hdmi.q && a.hdmi.q_mode == b.hdmi.q_mode && a.hdmi.k == b.hdmi.k &&
           a.hdmi.p == b.hdmi.p && a.hdmi.cf == b.hdmi.cf;
  } else {
    return a.dp_rate == b.dp_rate;
  }
}

void Controller::EnableBacklight(bool enable) {
  if (flags_ & FLAGS_BACKLIGHT) {
    uint32_t tmp = mmio_space()->Read32(BACKLIGHT_CTRL_OFFSET);

    if (enable) {
      tmp |= BACKLIGHT_CTRL_BIT;
    } else {
      tmp &= ~BACKLIGHT_CTRL_BIT;
    }

    mmio_space()->Write32(BACKLIGHT_CTRL_OFFSET, tmp);
  }
}

void Controller::HandleHotplug(registers::Ddi ddi, bool long_pulse) {
  LOG_TRACE("Hotplug detected on ddi %d (long_pulse=%d)\n", ddi, long_pulse);
  std::unique_ptr<DisplayDevice> device = nullptr;
  DisplayDevice* added_device = nullptr;
  uint64_t display_removed = INVALID_DISPLAY_ID;

  fbl::AutoLock lock(&display_lock_);

  for (size_t i = 0; i < display_devices_.size(); i++) {
    if (display_devices_[i]->ddi() == ddi) {
      if (display_devices_[i]->HandleHotplug(long_pulse)) {
        LOG_SPEW("hotplug handled by device\n");
        return;
      }
      device = display_devices_.erase(i);
      break;
    }
  }
  if (device) {  // Existing device was unplugged
    LOG_INFO("Display %ld unplugged\n", device->id());
    display_removed = device->id();
    // Make sure the display's resources get freed before reallocating the pipe buffers
    device.reset();
  } else {  // New device was plugged in
    std::unique_ptr<DisplayDevice> device = QueryDisplay(ddi);
    if (!device || !device->Init()) {
      LOG_INFO("failed to init hotplug display\n");
    } else {
      DisplayDevice* device_ptr = device.get();
      if (AddDisplay(std::move(device)) == ZX_OK) {
        added_device = device_ptr;
      }
    }
  }

  if (dc_intf_.is_valid() && (added_device || display_removed != INVALID_DISPLAY_ID)) {
    CallOnDisplaysChanged(&added_device, added_device != nullptr ? 1 : 0, &display_removed,
                          display_removed != INVALID_DISPLAY_ID);
  }
}

void Controller::HandlePipeVsync(registers::Pipe pipe, zx_time_t timestamp) {
  fbl::AutoLock lock(&display_lock_);

  if (!dc_intf_.is_valid()) {
    return;
  }

  uint64_t id = INVALID_DISPLAY_ID;
  // Plane 3 and the cursor are mutually exclusive, so this won't overflow.
  uint64_t handles[3];
  size_t handle_count = 0;
  if (pipes_[pipe].in_use()) {
    id = pipes_[pipe].attached_display_id();

    registers::PipeRegs regs(pipe);
    for (int i = 0; i < 3; i++) {
      auto live_surface = regs.PlaneSurfaceLive(i).ReadFrom(mmio_space());
      uint64_t handle = live_surface.surface_base_addr() << live_surface.kPageShift;
      if (handle) {
        handles[handle_count++] = handle;
      }
    }

    auto live_surface = regs.CursorSurfaceLive().ReadFrom(mmio_space());
    uint64_t handle = live_surface.surface_base_addr() << live_surface.kPageShift;
    if (handle) {
      handles[handle_count++] = handle;
    }
  }

  if (id != INVALID_DISPLAY_ID && handle_count) {
    dc_intf_.OnDisplayVsync(id, timestamp, handles, handle_count);
  }
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
  auto nde_rstwrn_opt = registers::NorthDERestetWarning::Get().ReadFrom(mmio_space());
  nde_rstwrn_opt.set_rst_pch_handshake_enable(1);
  nde_rstwrn_opt.WriteTo(mmio_space());

  // Wait for Power Well 0 distribution
  if (!WAIT_ON_US(registers::FuseStatus::Get().ReadFrom(mmio_space()).pg0_dist_status(), 5)) {
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
  auto dpll_enable = registers::DpllEnable::Get(registers::DPLL_0).ReadFrom(mmio_space());
  if (!dpll_enable.enable_dpll()) {
    // Set the cd_clk frequency to the minimum
    auto cd_clk = registers::CdClockCtl::Get().ReadFrom(mmio_space());
    cd_clk.set_cd_freq_select(cd_clk.kFreqSelect3XX);
    cd_clk.set_cd_freq_decimal(cd_clk.kFreqDecimal3375);
    cd_clk.WriteTo(mmio_space());

    // Configure DPLL0
    auto dpll_ctl1 = registers::DpllControl1::Get().ReadFrom(mmio_space());
    dpll_ctl1.dpll_link_rate(registers::DPLL_0).set(dpll_ctl1.kLinkRate810Mhz);
    dpll_ctl1.dpll_override(registers::DPLL_0).set(1);
    dpll_ctl1.dpll_hdmi_mode(registers::DPLL_0).set(0);
    dpll_ctl1.dpll_ssc_enable(registers::DPLL_0).set(0);
    dpll_ctl1.WriteTo(mmio_space());

    // Enable DPLL0 and wait for it
    dpll_enable.set_enable_dpll(1);
    dpll_enable.WriteTo(mmio_space());
    if (!WAIT_ON_MS(registers::Lcpll1Control::Get().ReadFrom(mmio_space()).pll_lock(), 5)) {
      LOG_ERROR("Failed to configure dpll0\n");
      return false;
    }

    // Do the magic sequence for Changing CD Clock Frequency specified on
    // intel-gfx-prm-osrc-skl-vol12-display.pdf p.135
    constexpr uint32_t kGtDriverMailboxInterface = 0x138124;
    constexpr uint32_t kGtDriverMailboxData0 = 0x138128;
    constexpr uint32_t kGtDriverMailboxData1 = 0x13812c;
    mmio_space()->Write32(kGtDriverMailboxData0, 0x3);
    mmio_space()->Write32(kGtDriverMailboxData1, 0x0);
    mmio_space()->Write32(kGtDriverMailboxInterface, 0x80000007);

    int count = 0;
    for (;;) {
      if (!WAIT_ON_US(mmio_space()->Read32(kGtDriverMailboxInterface) & 0x80000000, 150)) {
        LOG_ERROR("GT Driver Mailbox driver busy\n");
        return false;
      }
      if (mmio_space()->Read32(kGtDriverMailboxData0) & 0x1) {
        break;
      }
      if (count++ == 3) {
        LOG_ERROR("Failed to set cd_clk\n");
        return false;
      }
      zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
    }

    cd_clk.WriteTo(mmio_space());

    mmio_space()->Write32(kGtDriverMailboxData0, 0x3);
    mmio_space()->Write32(kGtDriverMailboxData1, 0x0);
    mmio_space()->Write32(kGtDriverMailboxInterface, 0x80000007);
  }

  // Enable and wait for DBUF
  auto dbuf_ctl = registers::DbufCtl::Get().ReadFrom(mmio_space());
  dbuf_ctl.set_power_request(1);
  dbuf_ctl.WriteTo(mmio_space());

  if (!WAIT_ON_US(registers::DbufCtl::Get().ReadFrom(mmio_space()).power_state(), 10)) {
    LOG_ERROR("Failed to enable DBUF\n");
    return false;
  }

  // We never use VGA, so just disable it at startup
  constexpr uint16_t kSequencerIdx = 0x3c4;
  constexpr uint16_t kSequencerData = 0x3c5;
  constexpr uint8_t kClockingModeIdx = 1;
  constexpr uint8_t kClockingModeScreenOff = (1 << 5);
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
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

  return true;
}

void Controller::ResetPipe(registers::Pipe pipe) {
  registers::PipeRegs pipe_regs(pipe);

  // Disable planes, bottom color, and cursor
  for (int i = 0; i < 3; i++) {
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

  if (was_enabled &&
      !WAIT_ON_MS(ddi_regs.DdiBufControl().ReadFrom(mmio_space()).ddi_idle_status(), 8)) {
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

registers::Dpll Controller::SelectDpll(bool is_edp, const dpll_state_t& state) {
  registers::Dpll res = registers::DPLL_INVALID;
  if (is_edp) {
    ZX_ASSERT(!state.is_hdmi);
    if (dplls_[0].use_count == 0 || dplls_[0].state.dp_rate == state.dp_rate) {
      res = registers::DPLL_0;
    }
  } else {
    for (unsigned i = registers::kDpllCount - 1; i > 0; i--) {
      if (dplls_[i].use_count == 0) {
        res = static_cast<registers::Dpll>(i);
      } else if (CompareDpllStates(dplls_[i].state, state)) {
        res = static_cast<registers::Dpll>(i);
        break;
      }
    }
  }

  if (res != registers::DPLL_INVALID) {
    dplls_[res].state = state;
    dplls_[res].use_count++;
    LOG_SPEW("Selected DPLL %d\n", res);
  } else {
    LOG_WARN("Failed to allocate DPLL\n");
  }

  return res;
}

const dpll_state_t* Controller::GetDpllState(registers::Dpll dpll) {
  if (dplls_[dpll].use_count) {
    return &dplls_[dpll].state;
  }
  return nullptr;
}

std::unique_ptr<DisplayDevice> Controller::QueryDisplay(registers::Ddi ddi) {
  fbl::AllocChecker ac;
  if (igd_opregion_.SupportsDp(ddi)) {
    LOG_SPEW("Checking for displayport monitor\n");
    auto dp_disp = fbl::make_unique_checked<DpDisplay>(&ac, this, next_id_, ddi);
    if (ac.check() && reinterpret_cast<DisplayDevice*>(dp_disp.get())->Query()) {
      return dp_disp;
    }
  }
  if (igd_opregion_.SupportsHdmi(ddi) || igd_opregion_.SupportsDvi(ddi)) {
    LOG_SPEW("Checking for hdmi monitor\n");
    auto hdmi_disp = fbl::make_unique_checked<HdmiDisplay>(&ac, this, next_id_, ddi);
    if (ac.check() && reinterpret_cast<DisplayDevice*>(hdmi_disp.get())->Query()) {
      return hdmi_disp;
    }
  }

  return nullptr;
}

bool Controller::LoadHardwareState(registers::Ddi ddi, DisplayDevice* device) {
  registers::DdiRegs regs(ddi);

  if (!registers::PowerWellControl2::Get().ReadFrom(mmio_space()).ddi_io_power_state(ddi).get() ||
      !regs.DdiBufControl().ReadFrom(mmio_space()).ddi_buffer_enable()) {
    return false;
  }

  auto pipe = registers::PIPE_INVALID;
  if (ddi == registers::DDI_A) {
    registers::TranscoderRegs regs(registers::TRANS_EDP);
    auto ddi_func_ctrl = regs.DdiFuncControl().ReadFrom(mmio_space());

    if (ddi_func_ctrl.edp_input_select() == ddi_func_ctrl.kPipeA) {
      pipe = registers::PIPE_A;
    } else if (ddi_func_ctrl.edp_input_select() == ddi_func_ctrl.kPipeB) {
      pipe = registers::PIPE_B;
    } else if (ddi_func_ctrl.edp_input_select() == ddi_func_ctrl.kPipeC) {
      pipe = registers::PIPE_C;
    }
  } else {
    for (unsigned j = 0; j < registers::kPipeCount; j++) {
      auto transcoder = registers::kTrans[j];
      registers::TranscoderRegs regs(transcoder);
      if (regs.ClockSelect().ReadFrom(mmio_space()).trans_clock_select() == ddi + 1u &&
          regs.DdiFuncControl().ReadFrom(mmio_space()).ddi_select() == ddi) {
        pipe = registers::kPipes[j];
        break;
      }
    }
  }

  if (pipe == registers::PIPE_INVALID) {
    return false;
  }

  auto dpll_ctrl2 = registers::DpllControl2::Get().ReadFrom(mmio_space());
  if (dpll_ctrl2.ddi_clock_off(ddi).get()) {
    return false;
  }

  auto dpll = static_cast<registers::Dpll>(dpll_ctrl2.ddi_clock_select(ddi).get());
  auto dpll_enable = registers::DpllEnable::Get(dpll).ReadFrom(mmio_space());
  if (!dpll_enable.enable_dpll()) {
    return false;
  }

  auto dpll_ctrl1 = registers::DpllControl1::Get().ReadFrom(mmio_space());
  dplls_[dpll].use_count++;
  dplls_[dpll].state.is_hdmi = dpll_ctrl1.dpll_hdmi_mode(dpll).get();
  if (dplls_[dpll].state.is_hdmi) {
    auto dpll_cfg1 = registers::DpllConfig1::Get(dpll).ReadFrom(mmio_space());
    auto dpll_cfg2 = registers::DpllConfig2::Get(dpll).ReadFrom(mmio_space());

    dplls_[dpll].state.hdmi = {
        .dco_int = static_cast<uint16_t>(dpll_cfg1.dco_integer()),
        .dco_frac = static_cast<uint16_t>(dpll_cfg1.dco_fraction()),
        .q = static_cast<uint8_t>(dpll_cfg2.qdiv_ratio()),
        .q_mode = static_cast<uint8_t>(dpll_cfg2.qdiv_mode()),
        .k = static_cast<uint8_t>(dpll_cfg2.kdiv_ratio()),
        .p = static_cast<uint8_t>(dpll_cfg2.pdiv_ratio()),
        .cf = static_cast<uint8_t>(dpll_cfg2.central_freq()),
    };
  } else {
    dplls_[dpll].state.dp_rate = dpll_ctrl1.dpll_link_rate(dpll).get();
  }

  device->AttachPipe(&pipes_[pipe]);

  device->LoadActiveMode();

  return true;
}

void Controller::InitDisplays() {
  fbl::AutoLock lock(&display_lock_);
  BringUpDisplayEngine(false);

  for (uint32_t i = 0; i < registers::kDdiCount; i++) {
    auto disp_device = QueryDisplay(registers::kDdis[i]);
    if (disp_device) {
      AddDisplay(std::move(disp_device));
    }
  }

  if (display_devices_.size() == 0) {
    LOG_INFO("No displays detected\n");
  }

  for (unsigned i = 0; i < registers::kDpllCount; i++) {
    dplls_[i].use_count = 0;
  }

  // Make a note of what needs to be reset, so we can finish querying the hardware state
  // before touching it, and so we can make sure transcoders are reset before ddis.
  bool ddi_needs_reset[registers::kDdiCount] = {};
  DisplayDevice* device_needs_init[registers::kDdiCount] = {};
  for (unsigned i = 0; i < registers::kDdiCount; i++) {
    auto ddi = registers::kDdis[i];
    DisplayDevice* device = nullptr;
    for (auto& d : display_devices_) {
      if (d->ddi() == ddi) {
        device = d.get();
        break;
      }
    }

    if (device == nullptr) {
      ddi_needs_reset[ddi] = true;
    } else {
      if (!LoadHardwareState(ddi, device)) {
        ddi_needs_reset[ddi] = true;
        device_needs_init[ddi] = device;
      } else {
        device->InitBacklight();
      }
    }
  }

  // Reset any transcoders which aren't in use
  for (unsigned i = 0; i < registers::kTransCount; i++) {
    auto transcoder = registers::kTrans[i];
    auto pipe = registers::PIPE_INVALID;
    for (auto& p : pipes_) {
      if (p.in_use() && p.transcoder() == transcoder) {
        pipe = p.pipe();
        break;
      }
    }

    if (pipe == registers::PIPE_INVALID) {
      ResetTrans(transcoder);
    }
  }

  // Reset any ddis which don't have a restored display. If we failed to restore a
  // display, try to initialize it here.
  for (unsigned i = 0; i < registers::kDdiCount; i++) {
    if (!ddi_needs_reset[i]) {
      continue;
    }
    ResetDdi(static_cast<registers::Ddi>(i));

    DisplayDevice* device = device_needs_init[i];
    if (device && !device->Init()) {
      for (unsigned i = 0; i < display_devices_.size(); i++) {
        if (display_devices_[i].get() == device) {
          display_devices_.erase(i);
          break;
        }
      }
    }
  }
}

zx_status_t Controller::AddDisplay(std::unique_ptr<DisplayDevice>&& display) {
  fbl::AllocChecker ac;
  display_devices_.reserve(display_devices_.size() + 1, &ac);

  if (ac.check()) {
    display_devices_.push_back(std::move(display), &ac);
    ZX_ASSERT(ac.check());

    std::unique_ptr<DisplayDevice>& new_device = display_devices_[display_devices_.size() - 1];
    LOG_INFO("Display %ld connected\n", new_device->id());
  } else {
    LOG_WARN("Failed to add display device\n");
    return ZX_ERR_NO_MEMORY;
  }

  next_id_++;
  return ZX_OK;
}

void Controller::CallOnDisplaysChanged(DisplayDevice** added, size_t added_count, uint64_t* removed,
                                       size_t removed_count) {
  added_display_args_t added_args[added_count];
  added_display_info_t added_info[added_count];
  size_t added_actual;
  for (unsigned i = 0; i < added_count; i++) {
    added_args[i].display_id = added[i]->id();
    added_args[i].edid_present = true;
    added_args[i].panel.i2c_bus_id = added[i]->i2c_bus_id();
    added_args[i].pixel_format_list = supported_formats;
    added_args[i].pixel_format_count = static_cast<uint32_t>(std::size(supported_formats));
    added_args[i].cursor_info_list = cursor_infos;
    added_args[i].cursor_info_count = static_cast<uint32_t>(std::size(cursor_infos));
  }
  dc_intf_.OnDisplaysChanged(added_args, added_count, removed, removed_count, added_info,
                             added_count, &added_actual);
  if (added_count != added_actual) {
    LOG_WARN("%lu displays could not be added\n", added_count - added_actual);
  }
  for (unsigned i = 0; i < added_actual; i++) {
    added[i]->set_is_hdmi(added_info[i].is_hdmi_out);
  }
}

// DisplayControllerImpl methods

void Controller::DisplayControllerImplSetDisplayControllerInterface(
    const display_controller_interface_protocol_t* intf) {
  fbl::AutoLock lock(&display_lock_);
  dc_intf_ = ddk::DisplayControllerInterfaceProtocolClient(intf);

  if (ready_for_callback_ && display_devices_.size()) {
    DisplayDevice* added_displays[registers::kDdiCount];
    uint32_t size = static_cast<uint32_t>(display_devices_.size());
    for (unsigned i = 0; i < size; i++) {
      added_displays[i] = display_devices_[i].get();
    }
    CallOnDisplaysChanged(added_displays, size, NULL, 0);
  }
}

zx_status_t Controller::DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo,
                                                            size_t offset) {
  return ZX_ERR_NOT_SUPPORTED;
}

static bool ConvertPixelFormatToType(fuchsia_sysmem_PixelFormat format, uint32_t* type_out) {
  if (format.type != fuchsia_sysmem_PixelFormatType_BGRA32 &&
      format.type != fuchsia_sysmem_PixelFormatType_R8G8B8A8) {
    return false;
  }

  if (!format.has_format_modifier) {
    return false;
  }

  switch (format.format_modifier.value) {
    case fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_X_TILED:
      *type_out = IMAGE_TYPE_X_TILED;
      return true;

    case fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_Y_TILED:
      *type_out = IMAGE_TYPE_Y_LEGACY_TILED;
      return true;

    case fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_YF_TILED:
      *type_out = IMAGE_TYPE_YF_TILED;
      return true;

    case fuchsia_sysmem_FORMAT_MODIFIER_LINEAR:
      *type_out = IMAGE_TYPE_SIMPLE;
      return true;

    default:
      return false;
  }
}

zx_status_t Controller::DisplayControllerImplImportImage(image_t* image, zx_unowned_handle_t handle,
                                                         uint32_t index) {
  if (!(image->type == IMAGE_TYPE_SIMPLE || image->type == IMAGE_TYPE_X_TILED ||
        image->type == IMAGE_TYPE_Y_LEGACY_TILED || image->type == IMAGE_TYPE_YF_TILED)) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status, status2;
  fuchsia_sysmem_BufferCollectionInfo_2 collection_info;
  status =
      fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(handle, &status2, &collection_info);
  if (status != ZX_OK) {
    return status;
  }
  if (status2 != ZX_OK) {
    return status2;
  }

  zx::vmo vmo;
  if (index < collection_info.buffer_count) {
    vmo = zx::vmo(collection_info.buffers[index].vmo);
    collection_info.buffers[index].vmo = ZX_HANDLE_INVALID;
  }
  for (uint32_t i = 0; i < collection_info.buffer_count; ++i) {
    zx_handle_close(collection_info.buffers[i].vmo);
  }

  if (!collection_info.settings.has_image_format_constraints || !vmo) {
    LOG_ERROR("Invalid image format or index\n");
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint64_t offset = collection_info.buffers[index].vmo_usable_start;
  if (offset % PAGE_SIZE != 0) {
    LOG_ERROR("Invalid offset\n");
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t type;
  if (!ConvertPixelFormatToType(collection_info.settings.image_format_constraints.pixel_format,
                                &type)) {
    LOG_ERROR("Invalid pixel format modifier\n");
    return ZX_ERR_INVALID_ARGS;
  }
  if (image->type != type) {
    LOG_ERROR("Incompatible image type\n");
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
  std::unique_ptr<GttRegion> gtt_region;
  status = gtt_.AllocRegion(length, align, &gtt_region);
  if (status != ZX_OK) {
    return status;
  }

  // The vsync logic requires that images not have base == 0
  if (gtt_region->base() == 0) {
    std::unique_ptr<GttRegion> alt_gtt_region;
    zx_status_t status = gtt_.AllocRegion(length, align, &alt_gtt_region);
    if (status != ZX_OK) {
      return status;
    }
    gtt_region = std::move(alt_gtt_region);
  }

  status = gtt_region->PopulateRegion(vmo.release(), offset / PAGE_SIZE, length);
  if (status != ZX_OK) {
    return status;
  }

  image->handle = gtt_region->base();
  imported_images_.push_back(std::move(gtt_region));
  return ZX_OK;
}

void Controller::DisplayControllerImplReleaseImage(image_t* image) {
  fbl::AutoLock lock(&gtt_lock_);
  for (unsigned i = 0; i < imported_images_.size(); i++) {
    if (imported_images_[i]->base() == image->handle) {
      imported_images_[i]->ClearRegion();
      imported_images_.erase(i);
      return;
    }
  }
}

const std::unique_ptr<GttRegion>& Controller::GetGttRegion(uint64_t handle) {
  fbl::AutoLock lock(&gtt_lock_);
  for (auto& region : imported_images_) {
    if (region->base() == handle) {
      return region;
    }
  }
  ZX_ASSERT(false);
}

bool Controller::GetPlaneLayer(registers::Pipe pipe, uint32_t plane,
                               const display_config_t** configs, size_t display_count,
                               const layer_t** layer_out) {
  if (!pipes_[pipe].in_use()) {
    return false;
  }
  uint64_t disp_id = pipes_[pipe].attached_display_id();

  for (unsigned i = 0; i < display_count; i++) {
    const display_config_t* config = configs[i];
    if (config->display_id != disp_id) {
      continue;
    }
    bool has_color_layer = config->layer_count && config->layer_list[0]->type == LAYER_TYPE_COLOR;
    for (unsigned j = 0; j < config->layer_count; j++) {
      if (config->layer_list[j]->type == LAYER_TYPE_PRIMARY) {
        if (plane != (config->layer_list[j]->z_index - has_color_layer)) {
          continue;
        }
      } else if (config->layer_list[j]->type == LAYER_TYPE_CURSOR) {
        // Since the config is validated, we know the cursor is the
        // highest plane, so we don't care about the layer's z_index.
        if (plane != registers::kCursorPlane) {
          continue;
        }
      } else if (config->layer_list[j]->type == LAYER_TYPE_COLOR) {
        // color layers aren't a plane
        continue;
      } else {
        ZX_ASSERT(false);
      }
      *layer_out = config->layer_list[j];
      return true;
    }
  }
  return false;
}

uint16_t Controller::CalculateBuffersPerPipe(size_t display_count) {
  ZX_ASSERT(display_count < registers::kPipeCount);
  return static_cast<uint16_t>(registers::PlaneBufCfg::kBufferCount / display_count);
}

bool Controller::CalculateMinimumAllocations(
    const display_config_t** display_configs, size_t display_count,
    uint16_t min_allocs[registers::kPipeCount][registers::kImagePlaneCount]) {
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

      if (layer->type == LAYER_TYPE_CURSOR) {
        min_allocs[pipe_num][plane_num] = 8;
        continue;
      }

      ZX_ASSERT(layer->type == LAYER_TYPE_PRIMARY);
      const primary_layer_t* primary = &layer->cfg.primary;

      if (primary->image.type == IMAGE_TYPE_SIMPLE || primary->image.type == IMAGE_TYPE_X_TILED) {
        min_allocs[pipe_num][plane_num] = 8;
      } else {
        uint32_t plane_source_width;
        uint32_t min_scan_lines;
        uint32_t bytes_per_pixel = ZX_PIXEL_FORMAT_BYTES(primary->image.pixel_format);
        if (primary->transform_mode == FRAME_TRANSFORM_IDENTITY ||
            primary->transform_mode == FRAME_TRANSFORM_ROT_180) {
          plane_source_width = primary->src_frame.width;
          min_scan_lines = 8;
        } else {
          plane_source_width = primary->src_frame.height;
          min_scan_lines = 32 / bytes_per_pixel;
        }
        min_allocs[pipe_num][plane_num] = static_cast<uint16_t>(
            ((fbl::round_up(4u * plane_source_width * bytes_per_pixel, 512u) / 512u) *
             (min_scan_lines / 4)) +
            3);
        if (min_allocs[pipe_num][plane_num] < 8) {
          min_allocs[pipe_num][plane_num] = 8;
        }
      }
      total += min_allocs[pipe_num][plane_num];
    }

    if (total && total > CalculateBuffersPerPipe(display_count)) {
      min_allocs[pipe_num][0] = UINT16_MAX;
      success = false;
    }
  }

  return success;
}

void Controller::UpdateAllocations(
    const uint16_t min_allocs[registers::kPipeCount][registers::kImagePlaneCount],
    const uint64_t data_rate[registers::kPipeCount][registers::kImagePlaneCount]) {
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

        double blocks = buffers_per_pipe * static_cast<double>(data_rate[pipe_num][plane_num]) /
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

  // Do the actual allocation, using the buffers that are assigned to each pipe.
  for (unsigned pipe_num = 0; pipe_num < registers::kPipeCount; pipe_num++) {
    uint16_t start = pipe_buffers_[pipe_num].start;
    for (unsigned plane_num = 0; plane_num < registers::kImagePlaneCount; plane_num++) {
      auto cur = &plane_buffers_[pipe_num][plane_num];

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

void Controller::ReallocatePlaneBuffers(const display_config_t** display_configs,
                                        size_t display_count, bool reallocate_pipes) {
  if (display_count == 0) {
    // Deal with reallocation later, when there are actually displays
    return;
  }

  uint16_t min_allocs[registers::kPipeCount][registers::kImagePlaneCount];
  if (!CalculateMinimumAllocations(display_configs, display_count, min_allocs)) {
    // The allocation should have been checked, so this shouldn't fail
    ZX_ASSERT(false);
  }

  // Calculate the data rates and store the minimum allocations
  uint64_t data_rate[registers::kPipeCount][registers::kImagePlaneCount];
  for (unsigned pipe_num = 0; pipe_num < registers::kPipeCount; pipe_num++) {
    registers::Pipe pipe = registers::kPipes[pipe_num];
    for (unsigned plane_num = 0; plane_num < registers::kImagePlaneCount; plane_num++) {
      const layer_t* layer;
      if (!GetPlaneLayer(pipe, plane_num, display_configs, display_count, &layer)) {
        data_rate[pipe_num][plane_num] = 0;
      } else if (layer->type == LAYER_TYPE_PRIMARY) {
        const primary_layer_t* primary = &layer->cfg.primary;

        uint32_t scaled_width =
            primary->src_frame.width * primary->src_frame.width / primary->dest_frame.width;
        uint32_t scaled_height =
            primary->src_frame.height * primary->src_frame.height / primary->dest_frame.height;
        data_rate[pipe_num][plane_num] =
            scaled_width * scaled_height * ZX_PIXEL_FORMAT_BYTES(primary->image.pixel_format);
      } else if (layer->type == LAYER_TYPE_CURSOR) {
        // Use a tiny data rate so the cursor gets the minimum number of buffers
        data_rate[pipe_num][plane_num] = 1;
      } else {
        // Other layers don't use pipe/planes, so GetPlaneLayer should have returned false
        ZX_ASSERT(false);
      }
    }
  }

  if (initial_alloc_) {
    initial_alloc_ = false;
    reallocate_pipes = true;
  }

  buffer_allocation_t active_allocation[registers::kPipeCount];
  if (reallocate_pipes) {
    // Allocate buffers to each pipe, but save the old allocation to use
    // when progressively updating the allocation.
    memcpy(active_allocation, pipe_buffers_, sizeof(active_allocation));

    uint16_t buffers_per_pipe = CalculateBuffersPerPipe(display_count);
    int active_pipes = 0;
    for (unsigned pipe_num = 0; pipe_num < registers::kPipeCount; pipe_num++) {
      if (pipes_[pipe_num].in_use()) {
        pipe_buffers_[pipe_num].start = static_cast<uint16_t>(buffers_per_pipe * active_pipes);
        pipe_buffers_[pipe_num].end =
            static_cast<uint16_t>(pipe_buffers_[pipe_num].start + buffers_per_pipe);
        active_pipes++;
      } else {
        pipe_buffers_[pipe_num].start = pipe_buffers_[pipe_num].end = 0;
      }
      LOG_SPEW("Pipe %d buffers: [%d, %d)\n", pipe_num, pipe_buffers_[pipe_num].start,
               pipe_buffers_[pipe_num].end);
    }
  }

  // It's not necessary to flush the buffer changes since the pipe allocs didn't change
  UpdateAllocations(min_allocs, data_rate);

  if (reallocate_pipes) {
    DoPipeBufferReallocation(active_allocation);
  }
}

void Controller::DoPipeBufferReallocation(
    buffer_allocation_t active_allocation[registers::kPipeCount]) {
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

      if (active_alloc->start == goal_alloc->start && active_alloc->end == goal_alloc->end) {
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

          if ((other_active->start <= goal_alloc->start && goal_alloc->start < other_active->end) ||
              (other_active->start < goal_alloc->end && goal_alloc->end <= other_active->end)) {
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

bool Controller::CheckDisplayLimits(const display_config_t** display_configs, size_t display_count,
                                    uint32_t** layer_cfg_results) {
  for (unsigned i = 0; i < display_count; i++) {
    const display_config_t* config = display_configs[i];

    // The intel display controller doesn't support these flags
    if (config->mode.flags & (MODE_FLAG_ALTERNATING_VBLANK | MODE_FLAG_DOUBLE_CLOCKED)) {
      return false;
    }

    DisplayDevice* display = FindDevice(config->display_id);
    if (display == nullptr) {
      continue;
    }

    // Pipes don't support height of more than 4096. They support a width of up to
    // 2^14 - 1. However, planes don't support a width of more than 8192 and we need
    // to always be able to accept a single plane, fullscreen configuration.
    if (config->mode.v_addressable > 4096 || config->mode.h_addressable > 8192) {
      return false;
    }

    uint64_t max_pipe_pixel_rate;
    auto cd_freq = registers::CdClockCtl::Get().ReadFrom(mmio_space()).cd_freq_decimal();
    if (cd_freq == registers::CdClockCtl::kFreqDecimal30857) {
      max_pipe_pixel_rate = 308570000;
    } else if (cd_freq == registers::CdClockCtl::kFreqDecimal3375) {
      max_pipe_pixel_rate = 337500000;
    } else if (cd_freq == registers::CdClockCtl::kFreqDecimal432) {
      max_pipe_pixel_rate = 432000000;
    } else if (cd_freq == registers::CdClockCtl::kFreqDecimal450) {
      max_pipe_pixel_rate = 450000000;
    } else if (cd_freq == registers::CdClockCtl::kFreqDecimal540) {
      max_pipe_pixel_rate = 540000000;
    } else if (cd_freq == registers::CdClockCtl::kFreqDecimal61714) {
      max_pipe_pixel_rate = 617140000;
    } else if (cd_freq == registers::CdClockCtl::kFreqDecimal675) {
      max_pipe_pixel_rate = 675000000;
    } else {
      ZX_ASSERT(false);
    }

    // Either the pipe pixel rate or the link pixel rate can't support a simple
    // configuration at this display resolution.
    if (max_pipe_pixel_rate < config->mode.pixel_clock_10khz * 10000 ||
        !display->CheckPixelRate(config->mode.pixel_clock_10khz * 10000)) {
      return false;
    }

    // Compute the maximum pipe pixel rate with the desired scaling. If the max rate
    // is too low, then make the client do any downscaling itself.
    double min_plane_ratio = 1.0;
    for (unsigned i = 0; i < config->layer_count; i++) {
      if (config->layer_list[i]->type != LAYER_TYPE_PRIMARY) {
        continue;
      }
      primary_layer_t* primary = &config->layer_list[i]->cfg.primary;
      uint32_t src_width, src_height;
      get_posttransform_width(*config->layer_list[i], &src_width, &src_height);

      double downscale = std::max(1.0, 1.0 * src_height / primary->dest_frame.height) *
                         std::max(1.0, 1.0 * src_width / primary->dest_frame.width);
      double plane_ratio = 1.0 / downscale;
      min_plane_ratio = std::min(plane_ratio, min_plane_ratio);
    }

    max_pipe_pixel_rate =
        static_cast<uint64_t>(min_plane_ratio * static_cast<double>(max_pipe_pixel_rate));
    if (max_pipe_pixel_rate < config->mode.pixel_clock_10khz * 10000) {
      for (unsigned j = 0; j < config->layer_count; j++) {
        if (config->layer_list[j]->type != LAYER_TYPE_PRIMARY) {
          continue;
        }
        primary_layer_t* primary = &config->layer_list[j]->cfg.primary;
        uint32_t src_width, src_height;
        get_posttransform_width(*config->layer_list[j], &src_width, &src_height);

        if (src_height > primary->dest_frame.height || src_width > primary->dest_frame.width) {
          layer_cfg_results[i][j] |= CLIENT_FRAME_SCALE;
        }
      }
    }

    // TODO(stevensd): Check maximum memory read bandwidth, watermark
  }

  return true;
}

uint32_t Controller::DisplayControllerImplCheckConfiguration(
    const display_config_t** display_config, size_t display_count, uint32_t** layer_cfg_result,
    size_t* layer_cfg_result_count) {
  fbl::AutoLock lock(&display_lock_);

  if (display_count == 0) {
    // All displays off is supported
    return CONFIG_DISPLAY_OK;
  }

  uint64_t pipe_alloc[registers::kPipeCount];
  if (!CalculatePipeAllocation(display_config, display_count, pipe_alloc)) {
    return CONFIG_DISPLAY_TOO_MANY;
  }

  if (!CheckDisplayLimits(display_config, display_count, layer_cfg_result)) {
    return CONFIG_DISPLAY_UNSUPPORTED_MODES;
  }

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
      LOG_INFO("Got config with no display - assuming hotplug and skipping\n");
      continue;
    }

    bool merge_all = false;
    if (config->layer_count > 3) {
      merge_all = config->layer_count > 4 || config->layer_list[0]->type != LAYER_TYPE_COLOR;
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

    uint32_t total_scalers_needed = 0;
    for (unsigned j = 0; j < config->layer_count; j++) {
      switch (config->layer_list[j]->type) {
        case LAYER_TYPE_PRIMARY: {
          primary_layer_t* primary = &config->layer_list[j]->cfg.primary;
          if (primary->transform_mode == FRAME_TRANSFORM_ROT_90 ||
              primary->transform_mode == FRAME_TRANSFORM_ROT_270) {
            // Linear and x tiled images don't support 90/270 rotation
            if (primary->image.type == IMAGE_TYPE_SIMPLE ||
                primary->image.type == IMAGE_TYPE_X_TILED) {
              layer_cfg_result[i][j] |= CLIENT_TRANSFORM;
            }
          } else if (primary->transform_mode != FRAME_TRANSFORM_IDENTITY &&
                     primary->transform_mode != FRAME_TRANSFORM_ROT_180) {
            // Cover unsupported rotations
            layer_cfg_result[i][j] |= CLIENT_TRANSFORM;
          }

          uint32_t src_width, src_height;
          get_posttransform_width(*config->layer_list[j], &src_width, &src_height);

          // If the plane is too wide, force the client to do all composition
          // and just give us a simple configuration.
          uint32_t max_width;
          if (primary->image.type == IMAGE_TYPE_SIMPLE ||
              primary->image.type == IMAGE_TYPE_X_TILED) {
            max_width = 8192;
          } else {
            max_width = 4096;
          }
          if (src_width > max_width) {
            merge_all = true;
          }

          if (primary->dest_frame.width != src_width || primary->dest_frame.height != src_height) {
            float ratio = registers::PipeScalerCtrl::k7x5MaxRatio;
            uint32_t max_width = static_cast<uint32_t>(static_cast<float>(src_width) * ratio);
            uint32_t max_height = static_cast<uint32_t>(static_cast<float>(src_height) * ratio);
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
            bool using_c = pipe_alloc[registers::PIPE_C] == display->id();
            if ((total_scalers_needed + scalers_needed) >
                    (using_c ? registers::PipeScalerCtrl::kPipeCScalersAvailable
                             : registers::PipeScalerCtrl::kPipeABScalersAvailable) ||
                src_width > registers::PipeScalerCtrl::kMaxSrcWidthPx ||
                src_width < registers::PipeScalerCtrl::kMinSrcSizePx ||
                src_height < registers::PipeScalerCtrl::kMinSrcSizePx ||
                max_width < primary->dest_frame.width || max_height < primary->dest_frame.height) {
              layer_cfg_result[i][j] |= CLIENT_FRAME_SCALE;
            } else {
              total_scalers_needed += scalers_needed;
            }
          }
          break;
        }
        case LAYER_TYPE_CURSOR: {
          if (j != config->layer_count - 1) {
            layer_cfg_result[i][j] |= CLIENT_USE_PRIMARY;
          }
          const image_t* image = &config->layer_list[j]->cfg.cursor.image;
          if (image->type != IMAGE_TYPE_SIMPLE) {
            layer_cfg_result[i][j] |= CLIENT_USE_PRIMARY;
          }
          bool found = false;
          for (unsigned x = 0; x < std::size(cursor_infos) && !found; x++) {
            found = image->width == cursor_infos[x].width &&
                    image->height == cursor_infos[x].height &&
                    image->pixel_format == cursor_infos[x].format;
          }
          if (!found) {
            layer_cfg_result[i][j] |= CLIENT_USE_PRIMARY;
          }
          break;
        }
        case LAYER_TYPE_COLOR: {
          if (j != 0) {
            layer_cfg_result[i][j] |= CLIENT_USE_PRIMARY;
          }
          zx_pixel_format_t format = config->layer_list[j]->cfg.color.format;
          if (format != ZX_PIXEL_FORMAT_RGB_x888 && format != ZX_PIXEL_FORMAT_ARGB_8888) {
            layer_cfg_result[i][j] |= CLIENT_USE_PRIMARY;
          }
          break;
        }
        default:
          layer_cfg_result[i][j] |= CLIENT_USE_PRIMARY;
      }
    }

    if (merge_all) {
      layer_cfg_result[i][0] = CLIENT_MERGE_BASE;
      for (unsigned j = 1; j < config->layer_count; j++) {
        layer_cfg_result[i][j] = CLIENT_MERGE_SRC;
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
      ZX_ASSERT(pipes_[pipe_num].in_use());  // If the allocation failed, it should be in use
      uint64_t display_id = pipes_[pipe_num].attached_display_id();
      for (unsigned i = 0; i < display_count; i++) {
        if (display_config[i]->display_id != display_id) {
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

  return CONFIG_DISPLAY_OK;
}

bool Controller::CalculatePipeAllocation(const display_config_t** display_config,
                                         size_t display_count,
                                         uint64_t alloc[registers::kPipeCount]) {
  if (display_count > registers::kPipeCount) {
    return false;
  }
  memset(alloc, 0, sizeof(uint64_t) * registers::kPipeCount);
  // Keep any allocated pipes on the same display
  for (unsigned i = 0; i < display_count; i++) {
    DisplayDevice* display = FindDevice(display_config[i]->display_id);
    if (display != nullptr && display->pipe() != nullptr) {
      alloc[display->pipe()->pipe()] = display_config[i]->display_id;
    }
  }
  // Give unallocated pipes to displays that need them
  for (unsigned i = 0; i < display_count; i++) {
    DisplayDevice* display = FindDevice(display_config[i]->display_id);
    if (display != nullptr && display->pipe() == nullptr) {
      for (unsigned pipe_num = 0; pipe_num < registers::kPipeCount; pipe_num++) {
        if (!alloc[pipe_num]) {
          alloc[pipe_num] = display_config[i]->display_id;
          break;
        }
      }
    }
  }
  return true;
}

bool Controller::ReallocatePipes(const display_config_t** display_config, size_t display_count) {
  if (display_count == 0) {
    // If we were given an empty config, just wait until there's
    // a real config before doing anything.
    return false;
  }

  uint64_t pipe_alloc[registers::kPipeCount];
  if (!CalculatePipeAllocation(display_config, display_count, pipe_alloc)) {
    // Reallocations should only happen for validated configurations, so the
    // pipe allocation should always succeed.
    ZX_ASSERT(false);
    return false;
  }

  bool pipe_change = false;
  for (unsigned i = 0; i < display_devices_.size(); i++) {
    auto& display = display_devices_[i];
    const display_config_t* config = find_config(display->id(), display_config, display_count);

    Pipe* pipe = nullptr;
    if (config) {
      pipe = display->pipe();
      if (pipe == nullptr) {
        for (unsigned i = 0; i < registers::kPipeCount; i++) {
          if (pipe_alloc[i] == display->id()) {
            pipe = pipes_ + i;
            break;
          }
        }
      }
    }

    if (display->AttachPipe(pipe)) {
      pipe_change = true;
    }
  }

  return pipe_change;
}

void Controller::DisplayControllerImplApplyConfiguration(const display_config_t** display_config,
                                                         size_t display_count) {
  uint64_t fake_vsyncs[registers::kDdiCount];
  uint32_t fake_vsync_count = 0;

  fbl::AutoLock lock(&display_lock_);

  bool pipe_change = ReallocatePipes(display_config, display_count);
  ReallocatePlaneBuffers(display_config, display_count, pipe_change);

  for (unsigned i = 0; i < display_devices_.size(); i++) {
    auto& display = display_devices_[i];
    const display_config_t* config = find_config(display->id(), display_config, display_count);

    if (config != nullptr) {
      display->ApplyConfiguration(config);
    } else {
      if (display->pipe()) {
        ResetPipe(display->pipe()->pipe());
      }
    }

    // The hardware only gives vsyncs if at least one plane is enabled, so
    // fake one if we need to, to inform the client that we're done with the
    // images.
    if (!config || config->layer_count == 0) {
      fake_vsyncs[fake_vsync_count++] = display->id();
    }
  }

  if (dc_intf_.is_valid()) {
    zx_time_t now = fake_vsync_count ? zx_clock_get_monotonic() : 0;
    for (unsigned i = 0; i < fake_vsync_count; i++) {
      dc_intf_.OnDisplayVsync(fake_vsyncs[i], now, nullptr, 0);
    }
  }
}

zx_status_t Controller::DisplayControllerImplGetSysmemConnection(zx::channel connection) {
  zx_status_t status = sysmem_connect(&sysmem_, connection.release());
  if (status != ZX_OK) {
    LOG_ERROR("Could not connect to sysmem\n");
    return status;
  }

  return ZX_OK;
}

zx_status_t Controller::DisplayControllerImplSetBufferCollectionConstraints(
    const image_t* config, zx_unowned_handle_t collection) {
  fuchsia_sysmem_BufferCollectionConstraints constraints = {};
  constraints.usage.display = fuchsia_sysmem_displayUsageLayer;
  constraints.has_buffer_memory_constraints = true;
  fuchsia_sysmem_BufferMemoryConstraints& buffer_constraints =
      constraints.buffer_memory_constraints;
  buffer_constraints.min_size_bytes = 0;
  buffer_constraints.max_size_bytes = 0xffffffff;
  buffer_constraints.physically_contiguous_required = false;
  buffer_constraints.secure_required = false;
  buffer_constraints.ram_domain_supported = true;
  buffer_constraints.cpu_domain_supported = false;
  buffer_constraints.heap_permitted_count = 1;
  buffer_constraints.heap_permitted[0] = fuchsia_sysmem_HeapType_SYSTEM_RAM;
  unsigned image_constraints_count = 0;
  for (unsigned i = 0; i < countof(image_types); ++i) {
    // Skip if image type was specified and different from current type. This
    // makes it possible for a different participant to select preferred
    // modifiers.
    if (config->type && config->type != image_types[i]) {
      continue;
    }
    fuchsia_sysmem_ImageFormatConstraints& image_constraints =
        constraints.image_format_constraints[image_constraints_count++];
    switch (config->pixel_format) {
      case ZX_PIXEL_FORMAT_ARGB_8888:
      case ZX_PIXEL_FORMAT_RGB_x888:
        image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_BGRA32;
        break;
      case ZX_PIXEL_FORMAT_ABGR_8888:
      case ZX_PIXEL_FORMAT_BGR_888x:
        image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_R8G8B8A8;
        break;
    }
    image_constraints.pixel_format.has_format_modifier = true;
    switch (image_types[i]) {
      case IMAGE_TYPE_SIMPLE:
        image_constraints.pixel_format.format_modifier.value =
            fuchsia_sysmem_FORMAT_MODIFIER_LINEAR;
        image_constraints.bytes_per_row_divisor = 64;
        image_constraints.start_offset_divisor = 64;
        break;
      case IMAGE_TYPE_X_TILED:
        image_constraints.pixel_format.format_modifier.value =
            fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_X_TILED;
        image_constraints.start_offset_divisor = 4096;
        image_constraints.bytes_per_row_divisor = 1;  // Not meaningful
        break;
      case IMAGE_TYPE_Y_LEGACY_TILED:
        image_constraints.pixel_format.format_modifier.value =
            fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_Y_TILED;
        image_constraints.start_offset_divisor = 4096;
        image_constraints.bytes_per_row_divisor = 1;  // Not meaningful
        break;
      case IMAGE_TYPE_YF_TILED:
        image_constraints.pixel_format.format_modifier.value =
            fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_YF_TILED;
        image_constraints.start_offset_divisor = 4096;
        image_constraints.bytes_per_row_divisor = 1;  // Not meaningful
        break;
    }
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0].type = fuchsia_sysmem_ColorSpaceType_SRGB;
    image_constraints.min_coded_width = 0;
    image_constraints.max_coded_width = 0xffffffff;
    image_constraints.min_coded_height = 0;
    image_constraints.max_coded_height = 0xffffffff;
    image_constraints.min_bytes_per_row = 0;
    image_constraints.max_bytes_per_row = 0xffffffff;
    image_constraints.max_coded_width_times_coded_height = 0xffffffff;
    image_constraints.layers = 1;
    image_constraints.coded_width_divisor = 1;
    image_constraints.coded_height_divisor = 1;
    image_constraints.display_width_divisor = 1;
    image_constraints.display_height_divisor = 1;
  }
  constraints.image_format_constraints_count = image_constraints_count;

  zx_status_t status =
      fuchsia_sysmem_BufferCollectionSetConstraints(collection, true, &constraints);

  if (status != ZX_OK) {
    LOG_ERROR("Failed to set constraints");
    return status;
  }

  return ZX_OK;
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
    zx_status_t status = pci_map_bar_buffer(&pci_, pci_bar, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                            &mapped_bars_[pci_bar].mmio);
    if (status != ZX_OK) {
      return status;
    }
  }

  // TODO(fxbug.dev/56253): Add MMIO_PTR to cast.
  *addr_out = (void*)mapped_bars_[pci_bar].mmio.vaddr;
  *size_out = mapped_bars_[pci_bar].mmio.size;
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
    mmio_buffer_release(&mapped_bars_[pci_bar].mmio);
  }
  return ZX_OK;
}

zx_status_t Controller::GetPciBti(uint32_t index, zx_handle_t* bti_out) {
  return pci_get_bti(&pci_, index, bti_out);
}

zx_status_t Controller::RegisterInterruptCallback(const zx_intel_gpu_core_interrupt_t* callback,
                                                  uint32_t interrupt_mask) {
  return interrupts_.SetInterruptCallback(callback, interrupt_mask);
}

zx_status_t Controller::UnregisterInterruptCallback() {
  constexpr zx_intel_gpu_core_interrupt_t kNoCallback = {nullptr, nullptr};
  interrupts_.SetInterruptCallback(&kNoCallback, 0);
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
  std::unique_ptr<GttRegion> region;
  zx_status_t status =
      gtt_.AllocRegion(static_cast<uint32_t>(page_count * PAGE_SIZE), PAGE_SIZE, &region);
  if (status != ZX_OK) {
    return status;
  }
  *addr_out = region->base();

  imported_gtt_regions_.push_back(std::move(region));
  return ZX_OK;
}

zx_status_t Controller::GttFree(uint64_t addr) {
  fbl::AutoLock lock(&gtt_lock_);
  for (unsigned i = 0; i < imported_gtt_regions_.size(); i++) {
    if (imported_gtt_regions_[i]->base() == addr) {
      imported_gtt_regions_.erase(i)->ClearRegion();
      return ZX_OK;
    }
  }
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t Controller::GttClear(uint64_t addr) {
  fbl::AutoLock lock(&gtt_lock_);
  for (unsigned i = 0; i < imported_gtt_regions_.size(); i++) {
    if (imported_gtt_regions_[i]->base() == addr) {
      imported_gtt_regions_[i]->ClearRegion();
      return ZX_OK;
    }
  }
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t Controller::GttInsert(uint64_t addr, zx_handle_t buffer, uint64_t page_offset,
                                  uint64_t page_count) {
  fbl::AutoLock lock(&gtt_lock_);
  for (unsigned i = 0; i < imported_gtt_regions_.size(); i++) {
    if (imported_gtt_regions_[i]->base() == addr) {
      return imported_gtt_regions_[i]->PopulateRegion(buffer, page_offset, page_count * PAGE_SIZE,
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

// I2C methods

uint32_t Controller::GetBusCount() { return registers::kDdiCount * 2; }

static constexpr size_t kMaxTxSize = 255;
zx_status_t Controller::GetMaxTransferSize(uint32_t bus_id, size_t* out_size) {
  *out_size = kMaxTxSize;
  return ZX_OK;
}

zx_status_t Controller::SetBitrate(uint32_t bus_id, uint32_t bitrate) {
  // no-op for now
  return ZX_OK;
}

zx_status_t Controller::Transact(uint32_t bus_id, const i2c_impl_op_t* ops, size_t count) {
  for (unsigned i = 0; i < count; i++) {
    if (ops[i].data_size > kMaxTxSize) {
      return ZX_ERR_INVALID_ARGS;
    }
  }
  if (!ops[count - 1].stop) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (bus_id < registers::kDdiCount) {
    return gmbus_i2cs_[bus_id].I2cTransact(ops, count);
  } else if (bus_id < 2 * registers::kDdiCount) {
    bus_id -= registers::kDdiCount;
    return dp_auxs_[bus_id].I2cTransact(ops, count);
  } else {
    return ZX_ERR_NOT_FOUND;
  }
}

bool Controller::DpcdRead(registers::Ddi ddi, uint32_t addr, uint8_t* buf, size_t size) {
  return dp_auxs_[ddi].DpcdRead(addr, buf, size);
}

bool Controller::DpcdWrite(registers::Ddi ddi, uint32_t addr, const uint8_t* buf, size_t size) {
  return dp_auxs_[ddi].DpcdWrite(addr, buf, size);
}

// Ddk methods

void Controller::DdkUnbind(ddk::UnbindTxn txn) {
  device_async_remove(zx_gpu_dev_);

  {
    fbl::AutoLock lock(&display_lock_);
    display_devices_.reset();
  }

  txn.Reply();
}

void Controller::DdkRelease() {
  display_released_ = true;
  if (gpu_released_) {
    delete this;
  }
}

zx_status_t Controller::DdkGetProtocol(uint32_t proto_id, void* out) {
  if (proto_id == ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL) {
    auto ops = static_cast<display_controller_impl_protocol_t*>(out);
    ops->ctx = this;
    ops->ops = static_cast<display_controller_impl_protocol_ops_t*>(ddk_proto_ops_);
  } else if (proto_id == ZX_PROTOCOL_I2C_IMPL) {
    auto ops = static_cast<i2c_impl_protocol_t*>(out);
    ops->ctx = this;
    ops->ops = &i2c_ops;
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

void Controller::DdkSuspend(ddk::SuspendTxn txn) {
  // TODO(fxbug.dev/43204): Implement the suspend hook based on suspendtxn
  if (txn.suspend_reason() == DEVICE_SUSPEND_REASON_MEXEC) {
    uint32_t format, width, height, stride;
    // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
    if (zx_framebuffer_get_info(get_root_resource(), &format, &width, &height, &stride) != ZX_OK) {
      txn.Reply(ZX_OK, txn.requested_state());
      return;
    }

    // The bootloader framebuffer is most likely at the start of the display
    // controller's bar 2. Try to get that buffer working again across the
    // mexec by mapping gfx stolen memory to gaddr 0.

    auto bdsm_reg = registers::BaseDsm::Get().FromValue(0);
    zx_status_t status = pci_config_read32(&pci_, bdsm_reg.kAddr, bdsm_reg.reg_value_ptr());
    if (status != ZX_OK) {
      LOG_TRACE("Failed to read dsm base\n");
      txn.Reply(ZX_OK, txn.requested_state());
      return;
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
    mmio_buffer_t mmio;
    if (pci_map_bar_buffer(&pci_, 2, ZX_CACHE_POLICY_WRITE_COMBINING, &mmio) == ZX_OK) {
      // TODO(fxbug.dev/56253): Add MMIO_PTR to cast.
      memset((void*)mmio.vaddr, 0, fb_size);
      mmio_buffer_release(&mmio);
    }

    {
      fbl::AutoLock lock(&display_lock_);
      for (auto& display : display_devices_) {
        if (display->pipe() == nullptr) {
          continue;
        }
        // TODO(fxbug.dev/31310): Reset/scale the display to ensure the buffer displays properly
        registers::PipeRegs pipe_regs(display->pipe()->pipe());

        auto plane_stride = pipe_regs.PlaneSurfaceStride(0).ReadFrom(mmio_space());
        plane_stride.set_stride(width_in_tiles(IMAGE_TYPE_SIMPLE, width, format));
        plane_stride.WriteTo(mmio_space());

        auto plane_surface = pipe_regs.PlaneSurface(0).ReadFrom(mmio_space());
        plane_surface.set_surface_base_addr(0);
        plane_surface.WriteTo(mmio_space());
      }
    }
  }
  txn.Reply(ZX_OK, txn.requested_state());
}

void Controller::DdkResume(ddk::ResumeTxn txn) {
  fbl::AutoLock lock(&display_lock_);
  BringUpDisplayEngine(true);

  registers::PanelPowerDivisor::Get().FromValue(pp_divisor_val_).WriteTo(mmio_space());
  registers::PanelPowerOffDelay::Get().FromValue(pp_off_delay_val_).WriteTo(mmio_space());
  registers::PanelPowerOnDelay::Get().FromValue(pp_on_delay_val_).WriteTo(mmio_space());
  registers::SouthBacklightCtl1::Get()
      .FromValue(0)
      .set_polarity(sblc_polarity_)
      .WriteTo(mmio_space());
  registers::SouthBacklightCtl2::Get().FromValue(sblc_ctrl2_val_).WriteTo(mmio_space());
  registers::SChicken1::Get().FromValue(schicken1_val_).WriteTo(mmio_space());

  registers::DdiRegs(registers::DDI_A)
      .DdiBufControl()
      .ReadFrom(mmio_space())
      .set_ddi_a_lane_capability_control(ddi_a_lane_capability_control_)
      .WriteTo(mmio_space());

  for (auto& disp : display_devices_) {
    if (!disp->Resume()) {
      LOG_ERROR("Failed to resume display\n");
    }
  }

  interrupts_.Resume();

  txn.Reply(ZX_OK, DEV_POWER_STATE_D0, txn.requested_state());
}

// TODO(stevensd): Move this back into ::Bind once long-running binds don't
// break devmgr's suspend/mexec.
void Controller::FinishInit() {
  LOG_TRACE("i915: initializing displays\n");

  for (auto& pipe : pipes_) {
    pipe.Init();
  }

  InitDisplays();

  {
    fbl::AutoLock lock(&display_lock_);
    uint32_t size = static_cast<uint32_t>(display_devices_.size());
    if (size && dc_intf_.is_valid()) {
      DisplayDevice* added_displays[registers::kDdiCount];
      for (unsigned i = 0; i < size; i++) {
        added_displays[i] = display_devices_[i].get();
      }
      CallOnDisplaysChanged(added_displays, size, NULL, 0);
    }

    ready_for_callback_ = true;
  }

  interrupts_.FinishInit();

  // TODO remove when the gfxconsole moves to user space
  EnableBacklight(true);

  LOG_TRACE("i915: initialization done\n");
}

zx_status_t Controller::Bind(std::unique_ptr<i915::Controller>* controller_ptr) {
  LOG_TRACE("Binding to display controller\n");

  zx_status_t status = device_get_protocol(parent(), ZX_PROTOCOL_SYSMEM, &sysmem_);
  if (status != ZX_OK) {
    LOG_ERROR("Could not get Display SYSMEM protocol\n");
    return status;
  }

  if (device_get_protocol(parent_, ZX_PROTOCOL_PCI, &pci_)) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  pci_config_read16(&pci_, PCI_CONFIG_DEVICE_ID, &device_id_);
  LOG_TRACE("Device id %x\n", device_id_);
  if (device_id_ == INTEL_I915_BROADWELL_DID) {
    // TODO: this should be based on the specific target
    flags_ |= FLAGS_BACKLIGHT;
  }

  status = igd_opregion_.Init(&pci_);
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

  {
    fbl::AutoLock lock(&bar_lock_);
    fbl::AllocChecker ac;
    mmio_space_ = ddk::MmioBuffer(mapped_bars_[0].mmio);
  }

  for (unsigned i = 0; i < registers::kDdiCount; i++) {
    gmbus_i2cs_[i].set_mmio_space(mmio_space());
    dp_auxs_[i].set_mmio_space(mmio_space());
  }

  pp_divisor_val_ = registers::PanelPowerDivisor::Get().ReadFrom(mmio_space()).reg_value();
  pp_off_delay_val_ = registers::PanelPowerOffDelay::Get().ReadFrom(mmio_space()).reg_value();
  pp_on_delay_val_ = registers::PanelPowerOnDelay::Get().ReadFrom(mmio_space()).reg_value();
  sblc_ctrl2_val_ = registers::SouthBacklightCtl2::Get().ReadFrom(mmio_space()).reg_value();
  schicken1_val_ = registers::SChicken1::Get().ReadFrom(mmio_space()).reg_value();

  sblc_polarity_ = registers::SouthBacklightCtl1::Get().ReadFrom(mmio_space()).polarity();
  ddi_a_lane_capability_control_ = registers::DdiRegs(registers::DDI_A)
                                       .DdiBufControl()
                                       .ReadFrom(mmio_space())
                                       .ddi_a_lane_capability_control();

  LOG_TRACE("Initialzing hotplug\n");
  status = interrupts_.Init();
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
    return status;
  }

  LOG_TRACE("bind done\n");

  status = thrd_create_with_name(&init_thread_, finish_init, this, "i915-init-thread");
  if (status != ZX_OK) {
    LOG_ERROR("Failed to create init thread\n");
    DdkAsyncRemove();
    return status;
  }
  init_thrd_started_ = true;

  return ZX_OK;
}

Controller::Controller(zx_device_t* parent) : DeviceType(parent), interrupts_(this), power_(this) {
  mtx_init(&display_lock_, mtx_plain);
  mtx_init(&gtt_lock_, mtx_plain);
  mtx_init(&bar_lock_, mtx_plain);
}

Controller::~Controller() {
  if (init_thrd_started_) {
    thrd_join(init_thread_, nullptr);
  }

  interrupts_.Destroy();
  if (mmio_space()) {
    EnableBacklight(false);

    for (unsigned i = 0; i < registers::kPipeCount; i++) {
      fbl::AutoLock lock(&display_lock_);
      pipes_[i].Reset();
    }
  }
  // Release anything leaked by the gpu-core client.
  fbl::AutoLock lock(&bar_lock_);
  // Start at 1, because we treat bar 0 specially.
  for (unsigned i = 1; i < PCI_MAX_BAR_COUNT; i++) {
    if (mapped_bars_[i].count) {
      LOG_WARN("Leaked bar %d\n", i);
      mapped_bars_[i].count = 1;
      UnmapPciMmio(i);
    }
  }

  // bar 0 should have at most one ref left, otherwise log a leak like above and correct it.
  // We will leave it with one ref, because mmio_space_ will unmap it on destruction, and
  // we may need to access mmio_space_ while destroying member variables.
  if (mapped_bars_[0].count != mmio_space_.has_value()) {
    LOG_WARN("Leaked bar 0\n");
    if (mapped_bars_[0].count > 0) {
      mapped_bars_[0].count = 1;
    }
  }
}

}  // namespace i915

zx_status_t intel_i915_bind(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  std::unique_ptr<i915::Controller> controller(new (&ac) i915::Controller(parent));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  return controller->Bind(&controller);
}

#define INTEL_I915_VID (0x8086)

static constexpr zx_driver_ops_t intel_i915_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = intel_i915_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(intel_i915, intel_i915_driver_ops, "zircon", "0.1", 28)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, INTEL_I915_VID),
    // Skylake DIDs
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x191b),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1912),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x191d),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1902),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1916),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x191e),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1906),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x190b),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1926),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1927),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1923),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x193b),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x192d),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x193d),
    // Kaby lake DIDs
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x5916),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x591c),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x591e),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x591b),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x5912),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x5926),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x5906),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x5927),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x5902),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x591a),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x591d),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x3ea5),
ZIRCON_DRIVER_END(intel_i915)
