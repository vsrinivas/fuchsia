// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/intel-i915-tgl.h"

#include <assert.h>
#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <fuchsia/hardware/intelgpucore/c/banjo.h>
#include <fuchsia/hardware/sysmem/c/banjo.h>
#include <inttypes.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/hw/inout.h>
#include <lib/device-protocol/pci.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/image-format/image_format.h>
#include <lib/zx/time.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <algorithm>
#include <future>
#include <iterator>
#include <memory>
#include <utility>

#include <fbl/vector.h>

#include "fbl/auto_lock.h"
#include "fuchsia/hardware/display/controller/c/banjo.h"
#include "src/graphics/display/drivers/intel-i915-tgl/clock/cdclk.h"
#include "src/graphics/display/drivers/intel-i915-tgl/ddi.h"
#include "src/graphics/display/drivers/intel-i915-tgl/dp-display.h"
#include "src/graphics/display/drivers/intel-i915-tgl/dpll.h"
#include "src/graphics/display/drivers/intel-i915-tgl/fuse-config.h"
#include "src/graphics/display/drivers/intel-i915-tgl/hdmi-display.h"
#include "src/graphics/display/drivers/intel-i915-tgl/intel-i915-tgl-bind.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pch-engine.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pci-ids.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pipe-manager.h"
#include "src/graphics/display/drivers/intel-i915-tgl/poll-until.h"
#include "src/graphics/display/drivers/intel-i915-tgl/power.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-dpll.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-pipe.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-transcoder.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers.h"
#include "src/graphics/display/drivers/intel-i915-tgl/tiling.h"

namespace {

constexpr zx_pixel_format_t kSupportedFormats[4] = {
    ZX_PIXEL_FORMAT_ARGB_8888, ZX_PIXEL_FORMAT_RGB_x888, ZX_PIXEL_FORMAT_ABGR_8888,
    ZX_PIXEL_FORMAT_BGR_888x};

constexpr cursor_info_t kCursorInfos[3] = {
    {.width = 64, .height = 64, .format = ZX_PIXEL_FORMAT_ARGB_8888},
    {.width = 128, .height = 128, .format = ZX_PIXEL_FORMAT_ARGB_8888},
    {.width = 256, .height = 256, .format = ZX_PIXEL_FORMAT_ARGB_8888},
};
constexpr uint32_t kImageTypes[4] = {
    IMAGE_TYPE_SIMPLE,
    IMAGE_TYPE_X_TILED,
    IMAGE_TYPE_Y_LEGACY_TILED,
    IMAGE_TYPE_YF_TILED,
};

constexpr fuchsia_sysmem::wire::PixelFormatType kPixelFormatTypes[2] = {
    fuchsia_sysmem::wire::PixelFormatType::kBgra32,
    fuchsia_sysmem::wire::PixelFormatType::kR8G8B8A8,
};

// TODO(fxbug.dev/85601): Remove after YUV buffers can be imported to Intel display.
constexpr fuchsia_sysmem::wire::PixelFormatType kYuvPixelFormatTypes[2] = {
    fuchsia_sysmem::wire::PixelFormatType::kI420,
    fuchsia_sysmem::wire::PixelFormatType::kNv12,
};

constexpr zx_protocol_device_t kGpuCoreDeviceProtocol = {
    .version = DEVICE_OPS_VERSION,
    .release = [](void* ctx) { static_cast<i915_tgl::Controller*>(ctx)->GpuRelease(); }
    // zx_gpu_dev_ is removed when unbind is called for zxdev() (in ::DdkUnbind),
    // so it's not necessary to give it its own unbind method.
};

constexpr zx_protocol_device_t kDisplayControllerDeviceProtocol = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol =
        [](void* ctx, uint32_t id, void* proto) {
          return device_get_protocol(reinterpret_cast<zx_device_t*>(ctx), id, proto);
        },
    .release = [](void* ctx) {},
};

// Can't be const because i2c_impl_protocol_t::ops is non-const.
i2c_impl_protocol_ops_t g_i2c_protocol_ops = {
    .get_bus_base = [](void* ctx) { return uint32_t{0}; },
    .get_bus_count =
        [](void* ctx) { return static_cast<i915_tgl::Controller*>(ctx)->GetBusCount(); },
    .get_max_transfer_size =
        [](void* ctx, uint32_t bus_id, size_t* out_size) {
          return static_cast<i915_tgl::Controller*>(ctx)->GetMaxTransferSize(bus_id, out_size);
        },
    .set_bitrate =
        [](void* ctx, uint32_t bus_id, uint32_t bitrate) {
          return static_cast<i915_tgl::Controller*>(ctx)->SetBitrate(bus_id, bitrate);
        },
    .transact =
        [](void* ctx, uint32_t bus_id, const i2c_impl_op_t* ops, size_t count) {
          return static_cast<i915_tgl::Controller*>(ctx)->Transact(bus_id, ops, count);
        },
};

const display_config_t* FindConfig(uint64_t display_id,
                                   cpp20::span<const display_config_t*> display_configs) {
  auto found =
      std::find_if(display_configs.begin(), display_configs.end(),
                   [display_id](const display_config_t* c) { return c->display_id == display_id; });
  return found != display_configs.end() ? *found : nullptr;
}

void GetPostTransformWidth(const layer_t& layer, uint32_t* width, uint32_t* height) {
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

struct FramebufferInfo {
  uint32_t size;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t format;
};

// The bootloader (UEFI and Depthcharge) informs zircon of the framebuffer information using a
// ZBI_TYPE_FRAMEBUFFER entry. We assume this information to be valid and unmodified by an
// unauthorized call to zx_framebuffer_set_range(), however this is potentially an issue.
// See fxbug.dev/77501.
zx::status<FramebufferInfo> GetFramebufferInfo() {
  FramebufferInfo info;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx_status_t status = zx_framebuffer_get_info(get_root_resource(), &info.format, &info.width,
                                               &info.height, &info.stride);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  info.size = info.stride * info.height * ZX_PIXEL_FORMAT_BYTES(info.format);
  return zx::ok(info);
}

}  // namespace

namespace i915_tgl {

void Controller::HandleHotplug(tgl_registers::Ddi ddi, bool long_pulse) {
  zxlogf(TRACE, "Hotplug detected on ddi %d (long_pulse=%d)", ddi, long_pulse);
  std::unique_ptr<DisplayDevice> device = nullptr;
  DisplayDevice* added_device = nullptr;
  uint64_t display_removed = INVALID_DISPLAY_ID;

  fbl::AutoLock lock(&display_lock_);

  for (size_t i = 0; i < display_devices_.size(); i++) {
    if (display_devices_[i]->ddi() == ddi) {
      if (display_devices_[i]->HandleHotplug(long_pulse)) {
        zxlogf(DEBUG, "hotplug handled by device");
        return;
      }
      device = display_devices_.erase(i);
      break;
    }
  }
  if (device) {  // Existing device was unplugged
    zxlogf(INFO, "Display %ld unplugged", device->id());
    display_removed = device->id();
    RemoveDisplay(std::move(device));
  } else {  // New device was plugged in
    std::unique_ptr<DisplayDevice> device = QueryDisplay(ddi, next_id_);
    if (!device || !device->Init()) {
      zxlogf(INFO, "failed to init hotplug display");
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

void Controller::HandlePipeVsync(tgl_registers::Pipe pipe_num, zx_time_t timestamp) {
  fbl::AutoLock lock(&display_lock_);

  if (!dc_intf_.is_valid()) {
    return;
  }

  uint64_t id = INVALID_DISPLAY_ID;

  std::optional<config_stamp_t> vsync_config_stamp = std::nullopt;

  Pipe* pipe = (*pipe_manager_)[pipe_num];
  if (pipe && pipe->in_use()) {
    id = pipe->attached_display_id();

    tgl_registers::PipeRegs regs(pipe_num);
    std::vector<uint64_t> handles;
    for (int i = 0; i < 3; i++) {
      auto live_surface = regs.PlaneSurfaceLive(i).ReadFrom(mmio_space());
      uint64_t handle = live_surface.surface_base_addr() << live_surface.kPageShift;

      if (handle) {
        handles.push_back(handle);
      }
    }

    auto live_surface = regs.CursorSurfaceLive().ReadFrom(mmio_space());
    uint64_t handle = live_surface.surface_base_addr() << live_surface.kPageShift;

    if (handle) {
      handles.push_back(handle);
    }

    vsync_config_stamp = pipe->GetVsyncConfigStamp(handles);
  }

  if (id != INVALID_DISPLAY_ID) {
    dc_intf_.OnDisplayVsync(id, timestamp,
                            vsync_config_stamp.has_value() ? &*vsync_config_stamp : nullptr);
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
  // We follow the steps in the PRM section "Mode Set" > "Sequences to
  // Initialize Display" > "Initialize Sequence", with the tweak that we attempt
  // to reuse the setup left in place by the boot firmware.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 pages 141-142
  // DG1: IHD-OS-DG1-Vol 12-2.21 pages 119-120
  // Kaby Lake: IHD-OS-KBL-Vol 12-1.17 page 112-113
  // Skylake: IHD-OS-SKL-Vol 12-05.16 page 110

  pch_engine_->SetPchResetHandshake(true);
  if (resume) {
    // The PCH clocks must be set during the display engine initialization
    // sequence. The rest of the PCH configuration will be restored later.
    pch_engine_->RestoreClockParameters();
  } else {
    const PchClockParameters pch_clock_parameters = pch_engine_->ClockParameters();
    PchClockParameters fixed_pch_clock_parameters = pch_clock_parameters;
    pch_engine_->FixClockParameters(fixed_pch_clock_parameters);
    if (pch_clock_parameters != fixed_pch_clock_parameters) {
      zxlogf(WARNING, "PCH clocking incorrectly configured. Re-configuring.");
    }
    pch_engine_->SetClockParameters(fixed_pch_clock_parameters);
  }

  // Wait for Power Well 0 distribution
  if (!PollUntil(
          [&] { return tgl_registers::FuseStatus::Get().ReadFrom(mmio_space()).pg0_dist_status(); },
          zx::usec(1), 20)) {
    zxlogf(ERROR, "Power Well 0 distribution failed");
    return false;
  }

  // TODO(fxbug.dev/109785): Currently the driver relies on the assumption that
  // PG1 and Misc IO are always enabled by firmware. We should manually ensure
  // them they are enabled here and disable them on driver teardown.

  ZX_DEBUG_ASSERT(power_);
  if (resume) {
    power_->Resume();
  } else {
    cd_clk_power_well_ = power_->GetCdClockPowerWellRef();
  }

  if (is_tgl(device_id_)) {
    auto pwr_well_ctrl = tgl_registers::PowerWellControl::Get().ReadFrom(mmio_space());
    pwr_well_ctrl.power_request(1).set(1);
    pwr_well_ctrl.WriteTo(mmio_space());

    if (!PollUntil(
            [&] {
              return tgl_registers::PowerWellControl::Get()
                  .ReadFrom(mmio_space())
                  .power_state(0)
                  .get();
            },
            zx::usec(1), 30)) {
      zxlogf(ERROR, "Power Well 1 state failed");
      return false;
    }

    if (!PollUntil(
            [&] {
              return tgl_registers::FuseStatus::Get().ReadFrom(mmio_space()).pg1_dist_status();
            },
            zx::usec(1), 20)) {
      zxlogf(ERROR, "Power Well 1 distribution failed");
      return false;
    }

    // Enable cd_clk and set the frequency to minimum.
    cd_clk_ = std::make_unique<CoreDisplayClockTigerLake>(mmio_space());
    // PLL ratio for 38.4MHz: 16 -> CDCLK 307.2 MHz
    if (!cd_clk_->SetFrequency(307'200)) {
      zxlogf(ERROR, "Failed to configure CD clock frequency");
      return false;
    }
  } else {
    // Enable CDCLK PLL to 337.5mhz if the BIOS didn't already enable it. If it needs to be
    // something special (i.e. for eDP), assume that the BIOS already enabled it.
    auto dpll_enable =
        tgl_registers::DpllEnable::GetForSkylakeDpll(tgl_registers::DPLL_0).ReadFrom(mmio_space());
    if (!dpll_enable.enable_dpll()) {
      // Configure DPLL0
      auto dpll_ctl1 = tgl_registers::DpllControl1::Get().ReadFrom(mmio_space());
      dpll_ctl1.SetLinkRate(tgl_registers::DPLL_0, tgl_registers::DpllControl1::LinkRate::k810Mhz);
      dpll_ctl1.dpll_override(tgl_registers::DPLL_0).set(1);
      dpll_ctl1.dpll_hdmi_mode(tgl_registers::DPLL_0).set(0);
      dpll_ctl1.dpll_ssc_enable(tgl_registers::DPLL_0).set(0);
      dpll_ctl1.WriteTo(mmio_space());

      // Enable DPLL0 and wait for it
      dpll_enable.set_enable_dpll(1);
      dpll_enable.WriteTo(mmio_space());
      if (!PollUntil(
              [&] { return tgl_registers::Lcpll1Control::Get().ReadFrom(mmio_space()).pll_lock(); },
              zx::usec(1), 5)) {
        zxlogf(ERROR, "Failed to configure dpll0");
        return false;
      }

      // Enable cd_clk and set the frequency to minimum.
      cd_clk_ = std::make_unique<CoreDisplayClockSkylake>(mmio_space());
      if (!cd_clk_->SetFrequency(337'500)) {
        zxlogf(ERROR, "Failed to configure CD clock frequency");
        return false;
      }
    } else {
      cd_clk_ = std::make_unique<CoreDisplayClockSkylake>(mmio_space());
      zxlogf(INFO, "CDCLK already assigned by BIOS: frequency: %u KHz",
             cd_clk_->current_freq_khz());
    }
  }

  // Power up DBUF (Data Buffer) slices.
  zxlogf(TRACE, "Powering up DBUF (Data Buffer) slices");
  const int display_buffer_slice_count = is_tgl(device_id_) ? 2 : 1;
  for (int slice_index = 0; slice_index < display_buffer_slice_count; ++slice_index) {
    auto display_buffer_control =
        tgl_registers::DataBufferControl::GetForSlice(slice_index).ReadFrom(mmio_space());
    display_buffer_control.set_powered_on_target(true).WriteTo(mmio_space());

    if (!PollUntil([&] { return display_buffer_control.ReadFrom(mmio_space()).powered_on(); },
                   zx::usec(1), 10)) {
      zxlogf(ERROR, "DBUF slice %d did not power up in time", slice_index + 1);
      return false;
    }
  }

  // We never use VGA, so just disable it at startup
  constexpr uint16_t kSequencerIdx = 0x3c4;
  constexpr uint16_t kSequencerData = 0x3c5;
  constexpr uint8_t kClockingModeIdx = 1;
  constexpr uint8_t kClockingModeScreenOff = (1 << 5);
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx_status_t status = zx_ioports_request(get_root_resource(), kSequencerIdx, 2);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to map vga ports");
    return false;
  }
  outp(kSequencerIdx, kClockingModeIdx);
  uint8_t clocking_mode = inp(kSequencerData);
  if (!(clocking_mode & kClockingModeScreenOff)) {
    outp(kSequencerIdx, inp(kSequencerData) | kClockingModeScreenOff);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));

    auto vga_ctl = tgl_registers::VgaCtl::Get().ReadFrom(mmio_space());
    vga_ctl.set_vga_display_disable(1);
    vga_ctl.WriteTo(mmio_space());
  }

  for (Pipe* pipe : *pipe_manager_) {
    pipe->Reset();
    ResetPipePlaneBuffers(pipe->pipe_id());

    tgl_registers::PipeRegs pipe_regs(pipe->pipe_id());

    // Disable the scalers (double buffered on PipeScalerWinSize), since
    // we don't know what state they are in at boot.
    pipe_regs.PipeScalerCtrl(0).ReadFrom(mmio_space()).set_enable(0).WriteTo(mmio_space());
    pipe_regs.PipeScalerWinSize(0).ReadFrom(mmio_space()).WriteTo(mmio_space());
    if (pipe->pipe_id() != tgl_registers::PIPE_C) {
      pipe_regs.PipeScalerCtrl(1).ReadFrom(mmio_space()).set_enable(0).WriteTo(mmio_space());
      pipe_regs.PipeScalerWinSize(1).ReadFrom(mmio_space()).WriteTo(mmio_space());
    }

    // Disable the cursor watermark
    for (int wm_num = 0; wm_num < 8; wm_num++) {
      auto wm = pipe_regs.PlaneWatermark(0, wm_num).FromValue(0);
      wm.WriteTo(mmio_space());
    }

    // Disable the primary plane watermarks and reset their buffer allocation
    for (unsigned plane_num = 0; plane_num < tgl_registers::kImagePlaneCount; plane_num++) {
      for (int wm_num = 0; wm_num < 8; wm_num++) {
        auto wm = pipe_regs.PlaneWatermark(plane_num + 1, wm_num).FromValue(0);
        wm.WriteTo(mmio_space());
      }
    }
  }

  return true;
}

void Controller::ResetPipePlaneBuffers(tgl_registers::Pipe pipe) {
  fbl::AutoLock lock(&plane_buffers_lock_);
  const int data_buffer_block_count = DataBufferBlockCount();
  for (unsigned plane_num = 0; plane_num < tgl_registers::kImagePlaneCount; plane_num++) {
    plane_buffers_[pipe][plane_num].start = data_buffer_block_count;
  }
}

bool Controller::ResetDdi(tgl_registers::Ddi ddi, std::optional<tgl_registers::Trans> transcoder) {
  tgl_registers::DdiRegs ddi_regs(ddi);

  // Disable the port
  auto ddi_buffer_control = ddi_regs.BufferControl().ReadFrom(mmio_space());
  const bool was_enabled = ddi_buffer_control.enabled();
  ddi_buffer_control.set_enabled(false).WriteTo(mmio_space());

  if (!is_tgl(device_id_)) {
    auto dp_transport_control = ddi_regs.DpTransportControl().ReadFrom(mmio_space());
    dp_transport_control.set_enabled(false)
        .set_training_pattern(tgl_registers::DpTransportControl::kTrainingPattern1)
        .WriteTo(mmio_space());
  } else {
    if (transcoder.has_value()) {
      auto dp_transport_control =
          tgl_registers::DpTransportControl::GetForTigerLakeTranscoder(*transcoder)
              .ReadFrom(mmio_space());
      dp_transport_control.set_enabled(false)
          .set_training_pattern(tgl_registers::DpTransportControl::kTrainingPattern1)
          .WriteTo(mmio_space());
    }
  }

  if (was_enabled && !PollUntil([&] { return ddi_buffer_control.ReadFrom(mmio_space()).is_idle(); },
                                zx::msec(1), 8)) {
    zxlogf(ERROR, "Port failed to go idle");
    return false;
  }

  // Disable IO power
  ZX_DEBUG_ASSERT(power_);
  power_->SetDdiIoPowerState(ddi, /* enable */ false);

  // Wait for DDI IO power to be fully disabled.
  // This step is not documented in Intel Display PRM, but this step occurs
  // in the drm/i915 driver and experiments on NUC11 hardware indicate that
  // display hotplug may fail without this step.
  if (!PollUntil([&] { return !power_->GetDdiIoPowerState(ddi); }, zx::usec(1), 1000)) {
    zxlogf(ERROR, "Disable IO power timeout");
    return false;
  }

  if (!dpll_manager_->Unmap(ddi)) {
    zxlogf(ERROR, "Failed to unmap DPLL for DDI %d", ddi);
    return false;
  }

  return true;
}

uint64_t Controller::SetupGttImage(const image_t* image, uint32_t rotation) {
  const std::unique_ptr<GttRegion>& region = GetGttRegion(image->handle);
  ZX_DEBUG_ASSERT(region);
  region->SetRotation(rotation, *image);
  return region->base();
}

std::unique_ptr<DisplayDevice> Controller::QueryDisplay(tgl_registers::Ddi ddi,
                                                        uint64_t display_id) {
  fbl::AllocChecker ac;
  if (!igd_opregion_.HasDdi(ddi)) {
    zxlogf(INFO, "ddi %d not available.", ddi);
    return nullptr;
  }

  if (igd_opregion_.SupportsDp(ddi)) {
    zxlogf(DEBUG, "Checking for DisplayPort monitor at DDI %d", ddi);
    auto dp_disp = fbl::make_unique_checked<DpDisplay>(&ac, this, display_id, ddi, &dp_auxs_[ddi],
                                                       &pch_engine_.value(), &root_node_);
    if (ac.check() && reinterpret_cast<DisplayDevice*>(dp_disp.get())->Query()) {
      return dp_disp;
    }
  }
  if (igd_opregion_.SupportsHdmi(ddi) || igd_opregion_.SupportsDvi(ddi)) {
    zxlogf(DEBUG, "Checking for HDMI monitor at DDI %d", ddi);
    auto hdmi_disp = fbl::make_unique_checked<HdmiDisplay>(&ac, this, display_id, ddi);
    if (ac.check() && reinterpret_cast<DisplayDevice*>(hdmi_disp.get())->Query()) {
      return hdmi_disp;
    }
  }
  zxlogf(TRACE, "Nothing found for ddi %d!", ddi);
  return nullptr;
}

bool Controller::LoadHardwareState(tgl_registers::Ddi ddi, DisplayDevice* device) {
  tgl_registers::DdiRegs regs(ddi);

  if (!power_->GetDdiIoPowerState(ddi) || !regs.BufferControl().ReadFrom(mmio_space()).enabled()) {
    return false;
  }

  auto dpll_state = dpll_manager()->LoadState(ddi);
  if (!dpll_state.has_value()) {
    zxlogf(ERROR, "Cannot load DPLL state for DDI %d", ddi);
    return false;
  }

  bool init_result = device->InitWithDpllState(&*dpll_state);
  if (!init_result) {
    zxlogf(ERROR, "Cannot initialize the display with DPLL state for DDI %d", ddi);
    return false;
  }

  device->LoadActiveMode();
  return true;
}

void Controller::InitDisplays() {
  fbl::AutoLock lock(&display_lock_);
  BringUpDisplayEngine(false);

  for (const auto ddi : ddis_) {
    auto disp_device = QueryDisplay(ddi, next_id_);
    if (disp_device) {
      AddDisplay(std::move(disp_device));
    }
  }

  if (display_devices_.size() == 0) {
    zxlogf(INFO, "intel-i915: No displays detected.");
  }

  // Make a note of what needs to be reset, so we can finish querying the hardware state
  // before touching it, and so we can make sure transcoders are reset before ddis.
  std::vector<std::pair<tgl_registers::Ddi, std::optional<tgl_registers::Trans>>>
      ddi_trans_needs_reset;
  std::vector<DisplayDevice*> device_needs_init;

  for (const auto ddi : ddis_) {
    DisplayDevice* device = nullptr;
    for (auto& d : display_devices_) {
      if (d->ddi() == ddi) {
        device = d.get();
        break;
      }
    }

    if (device == nullptr) {
      ddi_trans_needs_reset.emplace_back(ddi, std::nullopt);
    } else {
      if (!LoadHardwareState(ddi, device)) {
        auto transcoder_maybe = device->pipe()
                                    ? std::make_optional(device->pipe()->connected_transcoder_id())
                                    : std::nullopt;
        ddi_trans_needs_reset.emplace_back(ddi, transcoder_maybe);
        device_needs_init.push_back(device);
      } else {
        // On Tiger Lake, if a display device is already initialized by BIOS,
        // the pipe / transcoder / DDI should be all reset and reinitialized.
        // By doing this we can keep the display state fully controlled by the
        // driver.
        // TODO(fxbug.dev/111746): Consider doing this on all platforms.
        if (is_tgl(device_id())) {
          device_needs_init.push_back(device);
        }
        device->InitBacklight();
      }
    }
  }

  // Reset any transcoders which aren't in use
  pipe_manager_->ResetInactiveTranscoders();

  // Reset any ddis which don't have a restored display. If we failed to restore a
  // display, try to initialize it here.
  for (const auto& [ddi, transcoder_maybe] : ddi_trans_needs_reset) {
    ResetDdi(ddi, transcoder_maybe);
  }

  for (auto* device : device_needs_init) {
    ZX_ASSERT_MSG(device, "device_needs_init incorrectly populated above");
    for (unsigned i = 0; i < display_devices_.size(); i++) {
      if (display_devices_[i].get() == device) {
        if (is_tgl(device_id())) {
          // On Tiger Lake, devices pre-initialized by the BIOS must be reset
          // and reinitialized by the driver.
          // TODO(fxbug.dev/111747): We should fix the device reset logic so
          // that we don't need to delete the old device.
          const tgl_registers::Ddi ddi_id = device->ddi();
          const uint64_t display_id = device->id();
          display_devices_[i].reset();
          display_devices_[i] = QueryDisplay(ddi_id, display_id);
          if (!device || !device->Init()) {
            display_devices_.erase(i);
          }
        } else {
          if (!device->Init()) {
            display_devices_.erase(i);
          }
        }
        break;
      }
    }
  }
}

void Controller::RemoveDisplay(std::unique_ptr<DisplayDevice> display) {
  // Invalidate and disable any ELD.
  if (display->id() == eld_display_id_.value()) {
    auto audio_pin = tgl_registers::AudioPinEldCPReadyStatus::Get().ReadFrom(mmio_space());
    audio_pin.set_eld_valid_a(0).set_audio_enable_a(0).WriteTo(mmio_space());
    eld_display_id_.reset();
  }

  // Make sure the display's resources get freed before reallocating the pipe buffers by letting
  // "display" go out of scope.
}

zx_status_t Controller::AddDisplay(std::unique_ptr<DisplayDevice> display) {
  uint64_t display_id = display->id();

  // Add the new device.
  fbl::AllocChecker ac;
  display_devices_.push_back(std::move(display), &ac);
  if (!ac.check()) {
    zxlogf(WARNING, "Failed to add display device");
    return ZX_ERR_NO_MEMORY;
  }

  zxlogf(INFO, "Display %ld connected", display_id);
  next_id_++;
  return ZX_OK;
}

void Controller::CallOnDisplaysChanged(DisplayDevice** added, size_t added_count, uint64_t* removed,
                                       size_t removed_count) {
  size_t added_actual;
  added_display_args_t added_args[std::max(static_cast<size_t>(1), added_count)];
  added_display_info_t added_info[std::max(static_cast<size_t>(1), added_count)];
  for (unsigned i = 0; i < added_count; i++) {
    added_args[i].display_id = added[i]->id();
    added_args[i].edid_present = true;
    added_args[i].panel.i2c_bus_id = added[i]->i2c_bus_id();
    added_args[i].pixel_format_list = kSupportedFormats;
    added_args[i].pixel_format_count = static_cast<uint32_t>(std::size(kSupportedFormats));
    added_args[i].cursor_info_list = kCursorInfos;
    added_args[i].cursor_info_count = static_cast<uint32_t>(std::size(kCursorInfos));
  }
  dc_intf_.OnDisplaysChanged(added_args, added_count, removed, removed_count, added_info,
                             added_count, &added_actual);
  if (added_count != added_actual) {
    zxlogf(WARNING, "%lu displays could not be added", added_count - added_actual);
  }
  for (unsigned i = 0; i < added_actual; i++) {
    if (added[i]->type() == DisplayDevice::Type::kHdmi) {
      added[i]->set_type(added_info[i].is_hdmi_out ? DisplayDevice::Type::kHdmi
                                                   : DisplayDevice::Type::kDvi);
    }
  }
}

// DisplayControllerImpl methods

void Controller::DisplayControllerImplSetDisplayControllerInterface(
    const display_controller_interface_protocol_t* intf) {
  fbl::AutoLock lock(&display_lock_);
  dc_intf_ = ddk::DisplayControllerInterfaceProtocolClient(intf);

  if (ready_for_callback_ && display_devices_.size()) {
    uint32_t size = static_cast<uint32_t>(display_devices_.size());
    DisplayDevice* added_displays[size + 1];
    for (unsigned i = 0; i < size; i++) {
      added_displays[i] = display_devices_[i].get();
    }
    CallOnDisplaysChanged(added_displays, size, NULL, 0);
  }
}

static bool ConvertPixelFormatToType(fuchsia_sysmem::wire::PixelFormat format, uint32_t* type_out) {
  if (format.type != fuchsia_sysmem::wire::PixelFormatType::kBgra32 &&
      format.type != fuchsia_sysmem::wire::PixelFormatType::kR8G8B8A8) {
    return false;
  }

  if (!format.has_format_modifier) {
    return false;
  }

  switch (format.format_modifier.value) {
    case fuchsia_sysmem::wire::kFormatModifierIntelI915XTiled:
      *type_out = IMAGE_TYPE_X_TILED;
      return true;

    case fuchsia_sysmem::wire::kFormatModifierIntelI915YTiled:
      *type_out = IMAGE_TYPE_Y_LEGACY_TILED;
      return true;

    case fuchsia_sysmem::wire::kFormatModifierIntelI915YfTiled:
      *type_out = IMAGE_TYPE_YF_TILED;
      return true;

    case fuchsia_sysmem::wire::kFormatModifierLinear:
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
  auto result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_sysmem::BufferCollection>(
                                   zx::unowned_channel(handle)))
                    ->WaitForBuffersAllocated();
  if (!result.ok()) {
    zxlogf(ERROR, "Failed to wait for buffers allocated, %s", result.FormatDescription().c_str());
    return result.status();
  }
  if (result.value().status != ZX_OK) {
    return result.value().status;
  }

  fuchsia_sysmem::wire::BufferCollectionInfo2& collection_info =
      result.value().buffer_collection_info;

  if (!collection_info.settings.has_image_format_constraints) {
    zxlogf(ERROR, "No image format constraints");
    return ZX_ERR_INVALID_ARGS;
  }
  if (index >= collection_info.buffer_count) {
    zxlogf(ERROR, "Invalid index %d greater than buffer count %d", index,
           collection_info.buffer_count);
    return ZX_ERR_OUT_OF_RANGE;
  }

  zx::vmo vmo = std::move(collection_info.buffers[index].vmo);

  uint64_t offset = collection_info.buffers[index].vmo_usable_start;
  if (offset % PAGE_SIZE != 0) {
    zxlogf(ERROR, "Invalid offset");
    return ZX_ERR_INVALID_ARGS;
  }

  ZX_DEBUG_ASSERT(collection_info.settings.image_format_constraints.pixel_format.type !=
                      fuchsia_sysmem::wire::PixelFormatType::kI420 &&
                  collection_info.settings.image_format_constraints.pixel_format.type !=
                      fuchsia_sysmem::wire::PixelFormatType::kNv12);
  uint32_t type;
  if (!ConvertPixelFormatToType(collection_info.settings.image_format_constraints.pixel_format,
                                &type)) {
    zxlogf(ERROR, "Invalid pixel format modifier");
    return ZX_ERR_INVALID_ARGS;
  }
  if (image->type != type) {
    zxlogf(ERROR, "Incompatible image type from image %d and sysmem %d", image->type, type);
    return ZX_ERR_INVALID_ARGS;
  }

  fidl::Arena allocator;
  auto format_result = ImageFormatConvertZxToSysmem_v1(allocator, image->pixel_format);
  if (!format_result.is_ok()) {
    zxlogf(ERROR, "Pixel format %d can't be converted to sysmem", image->pixel_format);
    return ZX_ERR_INVALID_ARGS;
  }

  if (format_result.value().type !=
      collection_info.settings.image_format_constraints.pixel_format.type) {
    zxlogf(ERROR, "Sysmem pixel format from image %d doesn't match format from collection %d",
           format_result.value().type,
           collection_info.settings.image_format_constraints.pixel_format.type);
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&gtt_lock_);
  fbl::AllocChecker ac;
  imported_images_.reserve(imported_images_.size() + 1, &ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto format = ImageConstraintsToFormat(collection_info.settings.image_format_constraints,
                                         image->width, image->height);
  if (!format.is_ok()) {
    zxlogf(ERROR, "Failed to get format from constraints");
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t length = ImageFormatImageSize(format.value());

  ZX_DEBUG_ASSERT(length >= width_in_tiles(image->type, image->width, image->pixel_format) *
                                height_in_tiles(image->type, image->height, image->pixel_format) *
                                get_tile_byte_size(image->type));

  uint32_t align;
  if (image->type == IMAGE_TYPE_SIMPLE) {
    align = tgl_registers::PlaneSurface::kLinearAlignment;
  } else if (image->type == IMAGE_TYPE_X_TILED) {
    align = tgl_registers::PlaneSurface::kXTilingAlignment;
  } else {
    align = tgl_registers::PlaneSurface::kYTilingAlignment;
  }
  std::unique_ptr<GttRegion> gtt_region;
  zx_status_t status = gtt_.AllocRegion(length, align, &gtt_region);
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

bool Controller::GetPlaneLayer(Pipe* pipe, uint32_t plane,
                               cpp20::span<const display_config_t*> configs,
                               const layer_t** layer_out) {
  if (!pipe->in_use()) {
    return false;
  }
  uint64_t disp_id = pipe->attached_display_id();

  for (const display_config_t* config : configs) {
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
        if (plane != tgl_registers::kCursorPlane) {
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

uint16_t Controller::CalculateBuffersPerPipe(size_t active_pipe_count) {
  ZX_ASSERT(active_pipe_count < tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size());
  return static_cast<uint16_t>(DataBufferBlockCount() / active_pipe_count);
}

bool Controller::CalculateMinimumAllocations(
    cpp20::span<const display_config_t*> display_configs,
    uint16_t min_allocs[tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size()]
                       [tgl_registers::kImagePlaneCount]) {
  // This fn ignores layers after kImagePlaneCount. Displays with too many layers already
  // failed in ::CheckConfiguration, so it doesn't matter if we incorrectly say they pass here.
  bool success = true;
  for (Pipe* pipe : *pipe_manager_) {
    tgl_registers::Pipe pipe_num = pipe->pipe_id();
    uint32_t total = 0;

    for (unsigned plane_num = 0; plane_num < tgl_registers::kImagePlaneCount; plane_num++) {
      const layer_t* layer;
      if (!GetPlaneLayer(pipe, plane_num, display_configs, &layer)) {
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

    if (total && total > CalculateBuffersPerPipe(display_configs.size())) {
      min_allocs[pipe_num][0] = UINT16_MAX;
      success = false;
    }
  }

  return success;
}

void Controller::UpdateAllocations(
    const uint16_t min_allocs[tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size()]
                             [tgl_registers::kImagePlaneCount],
    const uint64_t data_rate[tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size()]
                            [tgl_registers::kImagePlaneCount]) {
  uint16_t allocs[tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size()]
                 [tgl_registers::kImagePlaneCount];

  for (unsigned pipe_num = 0;
       pipe_num < tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size(); pipe_num++) {
    uint64_t total_data_rate = 0;
    for (unsigned plane_num = 0; plane_num < tgl_registers::kImagePlaneCount; plane_num++) {
      total_data_rate += data_rate[pipe_num][plane_num];
    }
    if (total_data_rate == 0) {
      for (unsigned plane_num = 0; plane_num < tgl_registers::kImagePlaneCount; plane_num++) {
        allocs[pipe_num][plane_num] = 0;
      }
      continue;
    }

    // Allocate buffers based on the percentage of the total pixel bandwidth they take. If
    // that percentage isn't enough for a plane, give that plane its minimum allocation and
    // then try again.
    double buffers_per_pipe = pipe_buffers_[pipe_num].end - pipe_buffers_[pipe_num].start;
    bool forced_alloc[tgl_registers::kImagePlaneCount] = {};
    bool done = false;
    while (!done) {
      for (unsigned plane_num = 0; plane_num < tgl_registers::kImagePlaneCount; plane_num++) {
        if (forced_alloc[plane_num]) {
          continue;
        }

        double blocks = buffers_per_pipe * static_cast<double>(data_rate[pipe_num][plane_num]) /
                        static_cast<double>(total_data_rate);
        allocs[pipe_num][plane_num] = static_cast<uint16_t>(blocks);
      }

      done = true;

      for (unsigned plane_num = 0; plane_num < tgl_registers::kImagePlaneCount; plane_num++) {
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
  {
    fbl::AutoLock lock(&plane_buffers_lock_);
    const int data_buffer_block_count = DataBufferBlockCount();
    for (unsigned pipe_num = 0;
         pipe_num < tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size(); pipe_num++) {
      uint16_t start = pipe_buffers_[pipe_num].start;
      for (unsigned plane_num = 0; plane_num < tgl_registers::kImagePlaneCount; plane_num++) {
        auto cur = &plane_buffers_[pipe_num][plane_num];

        if (allocs[pipe_num][plane_num] == 0) {
          cur->start = data_buffer_block_count;
          cur->end = static_cast<uint16_t>(cur->start + 1);
        } else {
          cur->start = start;
          cur->end = static_cast<uint16_t>(start + allocs[pipe_num][plane_num]);
        }
        start = static_cast<uint16_t>(start + allocs[pipe_num][plane_num]);

        tgl_registers::Pipe pipe =
            tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>()[pipe_num];
        tgl_registers::PipeRegs pipe_regs(pipe);

        // These are latched on the surface address register, so we don't yet need to
        // worry about overlaps when updating planes during a pipe allocation.
        auto buf_cfg = pipe_regs.PlaneBufCfg(plane_num + 1).FromValue(0);
        buf_cfg.set_buffer_start(cur->start);
        buf_cfg.set_buffer_end(cur->end - 1);
        buf_cfg.WriteTo(mmio_space());

        // TODO(fxbug.com/111420): Follow the "Display Watermarks" guidelines.
        auto wm0 = pipe_regs.PlaneWatermark(plane_num + 1, 0).FromValue(0);
        wm0.set_enable(cur->start != data_buffer_block_count);
        wm0.set_blocks(cur->end - cur->start);
        wm0.WriteTo(mmio_space());

        // Give the buffers to both the cursor plane and plane 2, since
        // only one will actually be active.
        if (plane_num == tgl_registers::kCursorPlane) {
          auto buf_cfg = pipe_regs.PlaneBufCfg(0).FromValue(0);
          buf_cfg.set_buffer_start(cur->start);
          buf_cfg.set_buffer_end(cur->end - 1);
          buf_cfg.WriteTo(mmio_space());

          auto wm0 = pipe_regs.PlaneWatermark(0, 0).FromValue(0);
          wm0.set_enable(cur->start != data_buffer_block_count);
          wm0.set_blocks(cur->end - cur->start);
          wm0.WriteTo(mmio_space());
        }
      }
    }
  }
}

void Controller::ReallocatePlaneBuffers(cpp20::span<const display_config_t*> display_configs,
                                        bool reallocate_pipes) {
  if (display_configs.empty()) {
    // Deal with reallocation later, when there are actually displays
    return;
  }

  uint16_t min_allocs[tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size()]
                     [tgl_registers::kImagePlaneCount];
  if (!CalculateMinimumAllocations(display_configs, min_allocs)) {
    // The allocation should have been checked, so this shouldn't fail
    ZX_ASSERT(false);
  }

  // Calculate the data rates and store the minimum allocations
  uint64_t data_rate[tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size()]
                    [tgl_registers::kImagePlaneCount];
  for (Pipe* pipe : *pipe_manager_) {
    tgl_registers::Pipe pipe_num = pipe->pipe_id();
    for (unsigned plane_num = 0; plane_num < tgl_registers::kImagePlaneCount; plane_num++) {
      const layer_t* layer;
      if (!GetPlaneLayer(pipe, plane_num, display_configs, &layer)) {
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

  buffer_allocation_t
      active_allocation[tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size()];
  if (reallocate_pipes) {
    // Allocate buffers to each pipe, but save the old allocation to use
    // when progressively updating the allocation.
    memcpy(active_allocation, pipe_buffers_, sizeof(active_allocation));

    size_t active_pipes = std::count_if(pipe_manager_->begin(), pipe_manager_->end(),
                                        [](const Pipe* pipe) { return pipe->in_use(); });
    uint16_t buffers_per_pipe = CalculateBuffersPerPipe(active_pipes);

    int current_active_pipe = 0;
    for (Pipe* pipe : *pipe_manager_) {
      tgl_registers::Pipe pipe_num = pipe->pipe_id();
      if (pipe->in_use()) {
        pipe_buffers_[pipe_num].start =
            static_cast<uint16_t>(buffers_per_pipe * current_active_pipe);
        pipe_buffers_[pipe_num].end =
            static_cast<uint16_t>(pipe_buffers_[pipe_num].start + buffers_per_pipe);
        current_active_pipe++;
      } else {
        pipe_buffers_[pipe_num].start = pipe_buffers_[pipe_num].end = 0;
      }
      zxlogf(INFO, "Pipe %d buffers: [%d, %d)", pipe_num, pipe_buffers_[pipe_num].start,
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
    buffer_allocation_t
        active_allocation[tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size()]) {
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
    for (unsigned pipe_num = 0;
         pipe_num < tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size(); pipe_num++) {
      auto active_alloc = active_allocation + pipe_num;
      auto goal_alloc = pipe_buffers_ + pipe_num;

      if (active_alloc->start == goal_alloc->start && active_alloc->end == goal_alloc->end) {
        continue;
      }

      // Look through all the other active pipe allocations for overlap
      bool overlap = false;
      if (goal_alloc->start != goal_alloc->end) {
        for (unsigned other_pipe = 0;
             other_pipe < tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size();
             other_pipe++) {
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
        tgl_registers::PipeRegs pipe_regs(
            tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>()[pipe_num]);
        for (unsigned j = 0; j < tgl_registers::kImagePlaneCount; j++) {
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

bool Controller::CheckDisplayLimits(cpp20::span<const display_config_t*> display_configs,
                                    uint32_t** layer_cfg_results) {
  for (unsigned i = 0; i < display_configs.size(); i++) {
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
    auto cd_freq = tgl_registers::CdClockCtl::Get().ReadFrom(mmio_space()).cd_freq_decimal();

    if (cd_freq == tgl_registers::CdClockCtl::FreqDecimal(307200)) {
      max_pipe_pixel_rate = 307200000;
    } else if (cd_freq == tgl_registers::CdClockCtl::FreqDecimal(308570)) {
      max_pipe_pixel_rate = 308570000;
    } else if (cd_freq == tgl_registers::CdClockCtl::FreqDecimal(337500)) {
      max_pipe_pixel_rate = 337500000;
    } else if (cd_freq == tgl_registers::CdClockCtl::FreqDecimal(432000)) {
      max_pipe_pixel_rate = 432000000;
    } else if (cd_freq == tgl_registers::CdClockCtl::FreqDecimal(450000)) {
      max_pipe_pixel_rate = 450000000;
    } else if (cd_freq == tgl_registers::CdClockCtl::FreqDecimal(540000)) {
      max_pipe_pixel_rate = 540000000;
    } else if (cd_freq == tgl_registers::CdClockCtl::FreqDecimal(617140)) {
      max_pipe_pixel_rate = 617140000;
    } else if (cd_freq == tgl_registers::CdClockCtl::FreqDecimal(675000)) {
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
      GetPostTransformWidth(*config->layer_list[i], &src_width, &src_height);

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
        GetPostTransformWidth(*config->layer_list[j], &src_width, &src_height);

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
    const display_config_t** display_config, size_t display_config_count,
    uint32_t** layer_cfg_result, size_t* layer_cfg_result_count) {
  fbl::AutoLock lock(&display_lock_);

  cpp20::span display_configs(display_config, display_config_count);
  if (display_configs.empty()) {
    // All displays off is supported
    return CONFIG_DISPLAY_OK;
  }

  uint64_t pipe_alloc[tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size()];
  if (!CalculatePipeAllocation(display_configs, pipe_alloc)) {
    return CONFIG_DISPLAY_TOO_MANY;
  }

  if (!CheckDisplayLimits(display_configs, layer_cfg_result)) {
    return CONFIG_DISPLAY_UNSUPPORTED_MODES;
  }

  for (unsigned i = 0; i < display_configs.size(); i++) {
    auto* config = display_config[i];
    DisplayDevice* display = nullptr;
    for (auto& d : display_devices_) {
      if (d->id() == config->display_id) {
        display = d.get();
        break;
      }
    }
    if (display == nullptr) {
      zxlogf(INFO, "Got config with no display - assuming hotplug and skipping");
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
          GetPostTransformWidth(*config->layer_list[j], &src_width, &src_height);

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
            float ratio = tgl_registers::PipeScalerCtrl::k7x5MaxRatio;
            uint32_t max_width = static_cast<uint32_t>(static_cast<float>(src_width) * ratio);
            uint32_t max_height = static_cast<uint32_t>(static_cast<float>(src_height) * ratio);
            uint32_t scalers_needed = 1;
            // The 7x5 scaler (i.e. 2 scaler resources) is required if the src width is
            // >2048 and the required vertical scaling is greater than 1.99.
            if (primary->src_frame.width > 2048) {
              float ratio = tgl_registers::PipeScalerCtrl::kDynamicMaxVerticalRatio2049;
              uint32_t max_dynamic_height =
                  static_cast<uint32_t>(static_cast<float>(src_height) * ratio);
              if (max_dynamic_height < primary->dest_frame.height) {
                scalers_needed = 2;
              }
            }

            // Verify that there are enough scaler resources
            // Verify that the scaler input isn't too large or too small
            // Verify that the required scaling ratio isn't too large
            bool using_c = pipe_alloc[tgl_registers::PIPE_C] == display->id();
            if ((total_scalers_needed + scalers_needed) >
                    (using_c ? tgl_registers::PipeScalerCtrl::kPipeCScalersAvailable
                             : tgl_registers::PipeScalerCtrl::kPipeABScalersAvailable) ||
                src_width > tgl_registers::PipeScalerCtrl::kMaxSrcWidthPx ||
                src_width < tgl_registers::PipeScalerCtrl::kMinSrcSizePx ||
                src_height < tgl_registers::PipeScalerCtrl::kMinSrcSizePx ||
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
          for (unsigned x = 0; x < std::size(kCursorInfos) && !found; x++) {
            found = image->width == kCursorInfos[x].width &&
                    image->height == kCursorInfos[x].height &&
                    image->pixel_format == kCursorInfos[x].format;
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
  uint16_t arr[tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size()]
              [tgl_registers::kImagePlaneCount];
  if (!CalculateMinimumAllocations(display_configs, arr)) {
    // Find any displays whose allocation fails and set the return code. Overwrite
    // any previous errors, since they get solved by the merge.
    for (Pipe* pipe : *pipe_manager_) {
      tgl_registers::Pipe pipe_num = pipe->pipe_id();
      if (arr[pipe_num][0] != UINT16_MAX) {
        continue;
      }
      ZX_ASSERT(pipe->in_use());  // If the allocation failed, it should be in use
      uint64_t display_id = pipe->attached_display_id();
      for (unsigned i = 0; i < display_config_count; i++) {
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

bool Controller::CalculatePipeAllocation(
    cpp20::span<const display_config_t*> display_configs,
    uint64_t alloc[tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size()]) {
  if (display_configs.size() > tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size()) {
    return false;
  }
  memset(alloc, 0,
         sizeof(uint64_t) * tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size());
  // Keep any allocated pipes on the same display
  for (const display_config_t* config : display_configs) {
    DisplayDevice* display = FindDevice(config->display_id);
    if (display != nullptr && display->pipe() != nullptr) {
      alloc[display->pipe()->pipe_id()] = config->display_id;
    }
  }
  // Give unallocated pipes to displays that need them
  for (const display_config_t* config : display_configs) {
    DisplayDevice* display = FindDevice(config->display_id);
    if (display != nullptr && display->pipe() == nullptr) {
      for (unsigned pipe_num = 0;
           pipe_num < tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size();
           pipe_num++) {
        if (!alloc[pipe_num]) {
          alloc[pipe_num] = config->display_id;
          break;
        }
      }
    }
  }
  return true;
}

int Controller::DataBufferBlockCount() const {
  // Data buffer sizes are documented in the "Display Buffer Programming" >
  // "Display Buffer Size" section in the display engine PRMs.

  // Kaby Lake and Skylake display engines have a single DBUF slice with
  // 892 blocks.
  // Kaby Lake: IHD-OS-KBL-Vol 12-1.17 page 167
  // Skylake: IHD-OS-KBL-Vol 12-1.17 page 164
  static constexpr int kKabyLakeDataBufferBlockCount = 892;

  // Tiger Lake display engines have two DBUF slice with 1024 blocks each.
  // TODO(fxbug.dev/111716): We should be able to use 2048 blocks, since we
  // power up both slices.
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 page 297
  // DG1: IHD-OS-DG1-Vol 12-2.21 page 250
  static constexpr int kTigerLakeDataBufferBlockCount = 1023;

  return is_tgl(device_id_) ? kTigerLakeDataBufferBlockCount : kKabyLakeDataBufferBlockCount;
}

void Controller::DisplayControllerImplSetEld(uint64_t display_id, const uint8_t* raw_eld_list,
                                             size_t raw_eld_count) {
  // We use the first "a" of the 3 ELD slots in the datasheet.
  if (eld_display_id_.has_value() && eld_display_id_.value() != display_id) {
    zxlogf(ERROR, "ELD display already in use");
    return;
  }
  eld_display_id_ = display_id;

  constexpr size_t kMaxEldLength = 48;
  size_t length = std::min<size_t>(raw_eld_count, kMaxEldLength);
  auto edid0 = tgl_registers::AudEdidData::Get(0).ReadFrom(mmio_space());
  auto audio_pin = tgl_registers::AudioPinEldCPReadyStatus::Get().ReadFrom(mmio_space());
  auto ctrl = tgl_registers::AudioDipEldControlStatus::Get().ReadFrom(mmio_space());
  audio_pin.set_audio_enable_a(1).set_eld_valid_a(0).WriteTo(mmio_space());

  // TODO(andresoportus): We should "Wait for 2 vertical blanks" if we do this with the display
  // enabled.

  ctrl.set_eld_access_address(0).WriteTo(mmio_space());
  ZX_ASSERT(!(length % 4));  // We don't use vendor block so length is multiple of 4.
  for (size_t i = 0; i < length; i += 4) {
    edid0.set_data(raw_eld_list[i] | (raw_eld_list[i + 1] << 8) | (raw_eld_list[i + 2] << 16) |
                   (raw_eld_list[i + 3] << 24));
    edid0.WriteTo(mmio_space());
  }
  audio_pin.set_eld_valid_a(1).WriteTo(mmio_space());
}

void Controller::DisplayControllerImplApplyConfiguration(const display_config_t** display_config,
                                                         size_t display_config_count,
                                                         const config_stamp_t* config_stamp) {
  fbl::AutoLock lock(&display_lock_);
  uint64_t fake_vsync_display_ids[display_devices_.size() + 1];
  size_t fake_vsync_size = 0;

  cpp20::span display_configs(display_config, display_config_count);
  ReallocatePlaneBuffers(display_configs, /* reallocate_pipes */ pipe_manager_->PipeReallocated());

  for (auto& display : display_devices_) {
    const display_config_t* config = FindConfig(display->id(), display_configs);

    if (config != nullptr) {
      display->ApplyConfiguration(config, config_stamp);
    } else {
      if (display->pipe()) {
        // Only reset the planes so that it will display a blank screen.
        display->pipe()->ResetPlanes();
        ResetPipePlaneBuffers(display->pipe()->pipe_id());
      }
    }

    // The hardware only gives vsyncs if at least one plane is enabled, so
    // fake one if we need to, to inform the client that we're done with the
    // images.
    if (!config || config->layer_count == 0) {
      fake_vsync_display_ids[fake_vsync_size++] = display->id();
    }
  }

  if (dc_intf_.is_valid()) {
    zx_time_t now = (fake_vsync_size > 0) ? zx_clock_get_monotonic() : 0;
    for (size_t i = 0; i < fake_vsync_size; i++) {
      dc_intf_.OnDisplayVsync(fake_vsync_display_ids[i], now, config_stamp);
    }
  }
}

zx_status_t Controller::DisplayControllerImplGetSysmemConnection(zx::channel connection) {
  zx_status_t status = sysmem_connect(&sysmem_, connection.release());
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not connect to sysmem");
    return status;
  }

  return ZX_OK;
}

zx_status_t Controller::DisplayControllerImplSetBufferCollectionConstraints(
    const image_t* config, zx_unowned_handle_t collection) {
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints = {};
  constraints.usage.display = fuchsia_sysmem_displayUsageLayer;
  constraints.has_buffer_memory_constraints = true;
  fuchsia_sysmem::wire::BufferMemoryConstraints& buffer_constraints =
      constraints.buffer_memory_constraints;
  buffer_constraints.min_size_bytes = 0;
  buffer_constraints.max_size_bytes = 0xffffffff;
  buffer_constraints.physically_contiguous_required = false;
  buffer_constraints.secure_required = false;
  buffer_constraints.ram_domain_supported = true;
  buffer_constraints.cpu_domain_supported = false;
  buffer_constraints.heap_permitted_count = 1;
  buffer_constraints.heap_permitted[0] = fuchsia_sysmem::wire::HeapType::kSystemRam;
  unsigned image_constraints_count = 0;

  fuchsia_sysmem::wire::PixelFormatType pixel_format;
  switch (config->pixel_format) {
    case ZX_PIXEL_FORMAT_NONE:
      pixel_format = fuchsia_sysmem::wire::PixelFormatType::kInvalid;
      break;
    case ZX_PIXEL_FORMAT_ARGB_8888:
    case ZX_PIXEL_FORMAT_RGB_x888:
      pixel_format = fuchsia_sysmem::wire::PixelFormatType::kBgra32;
      break;
    case ZX_PIXEL_FORMAT_ABGR_8888:
    case ZX_PIXEL_FORMAT_BGR_888x:
      pixel_format = fuchsia_sysmem::wire::PixelFormatType::kR8G8B8A8;
      break;
    default:
      zxlogf(ERROR, "Config has unsupported pixel format %d", config->pixel_format);
      return ZX_ERR_INVALID_ARGS;
  }

  // Loop over all combinations of supported image types and pixel formats, adding
  // an image format constraints for each unless the config is asking for a specific
  // format or type.
  static_assert(std::size(kImageTypes) * std::size(kPixelFormatTypes) <=
                std::size(constraints.image_format_constraints));
  for (unsigned i = 0; i < std::size(kImageTypes); ++i) {
    // Skip if image type was specified and different from current type. This
    // makes it possible for a different participant to select preferred
    // modifiers.
    if (config->type && config->type != kImageTypes[i]) {
      continue;
    }
    for (unsigned j = 0; j < std::size(kPixelFormatTypes); ++j) {
      // Skip if pixel format was specified and different from current format.
      // This makes it possible for a different participant to select preferred
      // format.
      if (pixel_format != fuchsia_sysmem::wire::PixelFormatType::kInvalid &&
          pixel_format != kPixelFormatTypes[j]) {
        continue;
      }
      fuchsia_sysmem::wire::ImageFormatConstraints& image_constraints =
          constraints.image_format_constraints[image_constraints_count++];

      image_constraints.pixel_format.type = kPixelFormatTypes[j];
      image_constraints.pixel_format.has_format_modifier = true;
      switch (kImageTypes[i]) {
        case IMAGE_TYPE_SIMPLE:
          image_constraints.pixel_format.format_modifier.value =
              fuchsia_sysmem::wire::kFormatModifierLinear;
          image_constraints.bytes_per_row_divisor = 64;
          image_constraints.start_offset_divisor = 64;
          break;
        case IMAGE_TYPE_X_TILED:
          image_constraints.pixel_format.format_modifier.value =
              fuchsia_sysmem::wire::kFormatModifierIntelI915XTiled;
          image_constraints.start_offset_divisor = 4096;
          image_constraints.bytes_per_row_divisor = 1;  // Not meaningful
          break;
        case IMAGE_TYPE_Y_LEGACY_TILED:
          image_constraints.pixel_format.format_modifier.value =
              fuchsia_sysmem::wire::kFormatModifierIntelI915YTiled;
          image_constraints.start_offset_divisor = 4096;
          image_constraints.bytes_per_row_divisor = 1;  // Not meaningful
          break;
        case IMAGE_TYPE_YF_TILED:
          image_constraints.pixel_format.format_modifier.value =
              fuchsia_sysmem::wire::kFormatModifierIntelI915YfTiled;
          image_constraints.start_offset_divisor = 4096;
          image_constraints.bytes_per_row_divisor = 1;  // Not meaningful
          break;
      }
      image_constraints.color_spaces_count = 1;
      image_constraints.color_space[0].type = fuchsia_sysmem::wire::ColorSpaceType::kSrgb;
    }
  }
  if (image_constraints_count == 0) {
    zxlogf(ERROR, "Config has unsupported type %d", config->type);
    return ZX_ERR_INVALID_ARGS;
  }
  for (unsigned i = 0; i < std::size(kYuvPixelFormatTypes); ++i) {
    fuchsia_sysmem::wire::ImageFormatConstraints& image_constraints =
        constraints.image_format_constraints[image_constraints_count++];
    image_constraints.pixel_format.type = kYuvPixelFormatTypes[i];
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0].type = fuchsia_sysmem::wire::ColorSpaceType::kRec709;
  }
  constraints.image_format_constraints_count = image_constraints_count;

  auto result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_sysmem::BufferCollection>(
                                   zx::unowned_channel(collection)))
                    ->SetConstraints(true, constraints);

  if (!result.ok()) {
    zxlogf(ERROR, "Failed to set constraints, %s", result.FormatDescription().c_str());
    return result.status();
  }

  return ZX_OK;
}

// Intel GPU core methods

zx_status_t Controller::IntelGpuCoreReadPciConfig16(uint16_t addr, uint16_t* value_out) {
  return pci_.ReadConfig16(addr, value_out);
}

zx_status_t Controller::IntelGpuCoreMapPciMmio(uint32_t pci_bar, uint8_t** addr_out,
                                               uint64_t* size_out) {
  if (pci_bar > PCI_MAX_BAR_COUNT) {
    return ZX_ERR_INVALID_ARGS;
  }
  fbl::AutoLock lock(&bar_lock_);
  if (mapped_bars_[pci_bar].count == 0) {
    zx_status_t status =
        pci_.MapMmio(pci_bar, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mapped_bars_[pci_bar].mmio);
    if (status != ZX_OK) {
      return status;
    }
  }

  // TODO(fxbug.dev/56253): Add MMIO_PTR to cast.
  *addr_out = (uint8_t*)mapped_bars_[pci_bar].mmio.vaddr;
  *size_out = mapped_bars_[pci_bar].mmio.size;
  mapped_bars_[pci_bar].count++;
  return ZX_OK;
}

zx_status_t Controller::IntelGpuCoreUnmapPciMmio(uint32_t pci_bar) {
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

zx_status_t Controller::IntelGpuCoreGetPciBti(uint32_t index, zx::bti* bti_out) {
  return pci_.GetBti(index, bti_out);
}

zx_status_t Controller::IntelGpuCoreRegisterInterruptCallback(
    const intel_gpu_core_interrupt_t* callback, uint32_t interrupt_mask) {
  ZX_DEBUG_ASSERT(callback);
  return interrupts_.SetGpuInterruptCallback(*callback, interrupt_mask);
}

zx_status_t Controller::IntelGpuCoreUnregisterInterruptCallback() {
  constexpr intel_gpu_core_interrupt_t kNoCallback = {nullptr, nullptr};
  interrupts_.SetGpuInterruptCallback(kNoCallback, 0);
  return ZX_OK;
}

uint64_t Controller::IntelGpuCoreGttGetSize() {
  fbl::AutoLock lock(&gtt_lock_);
  return gtt_.size();
}

zx_status_t Controller::IntelGpuCoreGttAlloc(uint64_t page_count, uint64_t* addr_out) {
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

zx_status_t Controller::IntelGpuCoreGttFree(uint64_t addr) {
  fbl::AutoLock lock(&gtt_lock_);
  for (unsigned i = 0; i < imported_gtt_regions_.size(); i++) {
    if (imported_gtt_regions_[i]->base() == addr) {
      imported_gtt_regions_.erase(i)->ClearRegion();
      return ZX_OK;
    }
  }
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t Controller::IntelGpuCoreGttClear(uint64_t addr) {
  fbl::AutoLock lock(&gtt_lock_);
  for (unsigned i = 0; i < imported_gtt_regions_.size(); i++) {
    if (imported_gtt_regions_[i]->base() == addr) {
      imported_gtt_regions_[i]->ClearRegion();
      return ZX_OK;
    }
  }
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t Controller::IntelGpuCoreGttInsert(uint64_t addr, zx::vmo buffer, uint64_t page_offset,
                                              uint64_t page_count) {
  fbl::AutoLock lock(&gtt_lock_);
  for (unsigned i = 0; i < imported_gtt_regions_.size(); i++) {
    if (imported_gtt_regions_[i]->base() == addr) {
      return imported_gtt_regions_[i]->PopulateRegion(buffer.release(), page_offset,
                                                      page_count * PAGE_SIZE, true /* writable */);
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

uint32_t Controller::GetBusCount() { return ddis_.size() * 2; }

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

  size_t ddi_idx = bus_id >> 1;
  if (ddi_idx >= ddis_.size()) {
    return ZX_ERR_NOT_FOUND;
  }

  bool is_hdmi = bus_id & 1;
  if (is_hdmi) {
    return gmbus_i2cs_[ddi_idx].I2cTransact(ops, count);
  }
  return dp_auxs_[ddi_idx].I2cTransact(ops, count);
}

// Ddk methods

void Controller::DdkInit(ddk::InitTxn txn) {
  auto f = std::async(std::launch::async, [this, txn = std::move(txn)]() mutable {
    zxlogf(TRACE, "i915: initializing displays");

    {
      fbl::AutoLock lock(&display_lock_);
      for (Pipe* pipe : *pipe_manager_) {
        interrupts()->EnablePipeVsync(pipe->pipe_id(), true);
      }
    }

    InitDisplays();

    {
      fbl::AutoLock lock(&display_lock_);
      uint32_t size = static_cast<uint32_t>(display_devices_.size());
      if (size && dc_intf_.is_valid()) {
        DisplayDevice* added_displays[size + 1];
        for (unsigned i = 0; i < size; i++) {
          added_displays[i] = display_devices_[i].get();
        }
        CallOnDisplaysChanged(added_displays, size, NULL, 0);
      }

      ready_for_callback_ = true;
    }

    interrupts_.FinishInit();

    zxlogf(TRACE, "i915: display initialization done");
    txn.Reply(ZX_OK);
  });
}

void Controller::DdkUnbind(ddk::UnbindTxn txn) {
  device_async_remove(zx_gpu_dev_);
  device_async_remove(display_controller_dev_);

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
    ops->ops = static_cast<display_controller_impl_protocol_ops_t*>(
        &display_controller_impl_protocol_ops_);
    return ZX_OK;
  }

  if (proto_id == ZX_PROTOCOL_I2C_IMPL) {
    auto ops = static_cast<i2c_impl_protocol_t*>(out);
    ops->ctx = this;
    ops->ops = &g_i2c_protocol_ops;
    return ZX_OK;
  }

  return ZX_ERR_NOT_SUPPORTED;
}

void Controller::DdkSuspend(ddk::SuspendTxn txn) {
  // TODO(fxbug.dev/43204): Implement the suspend hook based on suspendtxn
  if (txn.suspend_reason() == DEVICE_SUSPEND_REASON_MEXEC) {
    zx::status<FramebufferInfo> fb_status = GetFramebufferInfo();
    if (fb_status.is_error()) {
      txn.Reply(ZX_OK, txn.requested_state());
      return;
    }

    // The bootloader framebuffer is most likely at the start of the display
    // controller's bar 2. Try to get that buffer working again across the
    // mexec by mapping gfx stolen memory to gaddr 0.

    auto bdsm_reg = tgl_registers::BaseDsm::Get().FromValue(0);
    zx_status_t status = pci_.ReadConfig32(bdsm_reg.kAddr, bdsm_reg.reg_value_ptr());
    if (status != ZX_OK) {
      zxlogf(TRACE, "Failed to read dsm base");
      txn.Reply(ZX_OK, txn.requested_state());
      return;
    }

    // The Intel docs say that the first page should be reserved for the gfx
    // hardware, but a lot of BIOSes seem to ignore that.
    uintptr_t fb = bdsm_reg.base_phys_addr() << bdsm_reg.base_phys_addr_shift;
    const auto& fb_info = fb_status.value();
    {
      fbl::AutoLock lock(&gtt_lock_);
      gtt_.SetupForMexec(fb, fb_info.size);
    }

    // Try to map the framebuffer and clear it. If not, oh well.
    mmio_buffer_t mmio;
    if (pci_.MapMmio(2, ZX_CACHE_POLICY_WRITE_COMBINING, &mmio) == ZX_OK) {
      // TODO(fxbug.dev/56253): Add MMIO_PTR to cast.
      memset((void*)mmio.vaddr, 0, fb_info.size);
      mmio_buffer_release(&mmio);
    }

    {
      fbl::AutoLock lock(&display_lock_);
      for (auto& display : display_devices_) {
        if (display->pipe() == nullptr) {
          continue;
        }
        // TODO(fxbug.dev/31310): Reset/scale the display to ensure the buffer displays properly
        tgl_registers::PipeRegs pipe_regs(display->pipe()->pipe_id());

        auto plane_stride = pipe_regs.PlaneSurfaceStride(0).ReadFrom(mmio_space());
        plane_stride.set_stride(width_in_tiles(IMAGE_TYPE_SIMPLE, fb_info.width, fb_info.format));
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

  pch_engine_->RestoreNonClockParameters();

  if (!is_tgl(device_id_)) {
    // TODO(fxbug.dev/109227): Intel's documentation states that this field
    // should only be written once, at system boot. Either delete this, or
    // document an experiment confirming that this write works as intended.
    //
    // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 444
    // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 440
    tgl_registers::DdiRegs(tgl_registers::DDI_A)
        .BufferControl()
        .ReadFrom(mmio_space())
        .set_ddi_e_disabled_kaby_lake(ddi_e_disabled_)
        .WriteTo(mmio_space());
  }

  for (auto& disp : display_devices_) {
    if (!disp->Resume()) {
      zxlogf(ERROR, "Failed to resume display");
    }
  }

  interrupts_.Resume();

  txn.Reply(ZX_OK, DEV_POWER_STATE_D0, txn.requested_state());
}

zx_status_t Controller::Init() {
  zxlogf(TRACE, "Binding to display controller");

  zx_status_t status = ZX_ERR_NOT_FOUND;
  if ((status = device_get_fragment_protocol(parent(), "sysmem", ZX_PROTOCOL_SYSMEM, &sysmem_)) !=
      ZX_OK) {
    zxlogf(ERROR, "Could not get Display SYSMEM protocol: %s", zx_status_get_string(status));
    return status;
  }

  pci_ = ddk::Pci(parent(), "pci");
  if (!pci_.is_valid()) {
    zxlogf(ERROR, "Could not get Display PCI protocol");
    return ZX_ERR_INTERNAL;
  }

  pci_.ReadConfig16(PCI_CONFIG_DEVICE_ID, &device_id_);
  zxlogf(TRACE, "Device id %x", device_id_);

  status = igd_opregion_.Init(pci_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to init VBT (%d)", status);
    return status;
  }

  zxlogf(TRACE, "Mapping registers");
  // map register window
  uint8_t* regs;
  uint64_t size;
  status = IntelGpuCoreMapPciMmio(0u, &regs, &size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to map bar 0: %d", status);
    return status;
  }

  {
    fbl::AutoLock lock(&bar_lock_);
    fbl::AllocChecker ac;
    mmio_space_ = fdf::MmioBuffer(mapped_bars_[0].mmio);
  }

  zxlogf(TRACE, "Reading fuses and straps");
  FuseConfig fuse_config = FuseConfig::ReadFrom(*mmio_space(), device_id_);
  fuse_config.Log();

  zxlogf(TRACE, "Initializing DDIs");
  ddis_ = GetDdis(device_id_);

  zxlogf(TRACE, "Initializing Power");
  power_ = Power::New(mmio_space(), device_id_);

  zxlogf(TRACE, "Reading PCH display engine config");
  pch_engine_.emplace(mmio_space(), device_id_);
  pch_engine_->Log();

  for (unsigned i = 0; i < ddis_.size(); i++) {
    gmbus_i2cs_.push_back(GMBusI2c(ddis_[i], mmio_space()));

    dp_auxs_.push_back(DpAux(mmio_space(), ddis_[i], device_id_));
    zxlogf(TRACE, "DDI %d AUX channel initial configuration:", ddis_[i]);
    dp_auxs_[dp_auxs_.size() - 1].aux_channel().Log();
  }

  if (!is_tgl(device_id_)) {
    ddi_e_disabled_ = tgl_registers::DdiRegs(tgl_registers::DDI_A)
                          .BufferControl()
                          .ReadFrom(mmio_space())
                          .ddi_e_disabled_kaby_lake();
  }

  zxlogf(TRACE, "Initializing interrupts");
  status = interrupts_.Init(fit::bind_member<&Controller::HandlePipeVsync>(this),
                            fit::bind_member<&Controller::HandleHotplug>(this), parent(), pci_,
                            mmio_space(), device_id_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to initialize interrupts");
    return status;
  }

  zxlogf(TRACE, "Mapping gtt");
  {
    // The bootloader framebuffer is located at the start of the BAR that gets mapped by GTT.
    // Prevent clients from allocating memory in this region by telling |gtt_| to exclude it from
    // the region allocator.
    uint32_t offset = 0u;
    auto fb = GetFramebufferInfo();
    if (fb.is_error()) {
      zxlogf(INFO, "Failed to obtain framebuffer size (%s)", fb.status_string());
      // It is possible for zx_framebuffer_get_info to fail in a headless system as the bootloader
      // framebuffer information will be left uninitialized. Tolerate this failure by assuming
      // that the stolen memory contents won't be shown on any screen and map the global GTT at
      // offset 0.
      offset = 0u;
    } else {
      offset = fb.value().size;
    }

    fbl::AutoLock lock(&gtt_lock_);
    status = gtt_.Init(pci_, mmio_space()->View(GTT_BASE_OFFSET), offset);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to init gtt (%s)", zx_status_get_string(status));
      return status;
    }
  }

  {
    fbl::AutoLock lock(&display_lock_);
    if (is_tgl(device_id())) {
      pipe_manager_ = std::make_unique<PipeManagerTigerLake>(this);
    } else {
      pipe_manager_ = std::make_unique<PipeManagerSkylake>(this);
    }
  }

  if (is_tgl(device_id())) {
    dpll_manager_ = std::make_unique<DpllManagerTigerLake>(mmio_space());
  } else {
    dpll_manager_ = std::make_unique<DpllManagerSkylake>(mmio_space());
  }

  status = DdkAdd(ddk::DeviceAddArgs("intel_i915")
                      .set_inspect_vmo(inspector_.DuplicateVmo())
                      .set_flags(DEVICE_ADD_NON_BINDABLE));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to add controller device");
    return status;
  }

  {
    device_add_args_t display_device_add_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "intel-display-controller",
        .ctx = zxdev(),
        .ops = &kDisplayControllerDeviceProtocol,
        .proto_id = ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL,
        .proto_ops = &display_controller_impl_protocol_ops_,
    };
    status = device_add(zxdev(), &display_device_add_args, &display_controller_dev_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to publish display controller device (%d)", status);
      return status;
    }
  }

  {
    device_add_args_t gpu_device_add_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "intel-gpu-core",
        .ctx = this,
        .ops = &kGpuCoreDeviceProtocol,
        .proto_id = ZX_PROTOCOL_INTEL_GPU_CORE,
        .proto_ops = &intel_gpu_core_protocol_ops_,
    };
    status = device_add(zxdev(), &gpu_device_add_args, &zx_gpu_dev_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to publish gpu core device (%d)", status);
      return status;
    }
  }

  root_node_ = inspector_.GetRoot().CreateChild("intel-i915");

  zxlogf(TRACE, "bind done");

  return ZX_OK;
}

Controller::Controller(zx_device_t* parent) : DeviceType(parent) {
  mtx_init(&display_lock_, mtx_plain);
  mtx_init(&gtt_lock_, mtx_plain);
  mtx_init(&bar_lock_, mtx_plain);
  mtx_init(&plane_buffers_lock_, mtx_plain);
}

Controller::~Controller() {
  interrupts_.Destroy();
  if (mmio_space() && pipe_manager_.get()) {
    for (Pipe* pipe : *pipe_manager_) {
      fbl::AutoLock lock(&display_lock_);
      interrupts()->EnablePipeVsync(pipe->pipe_id(), true);
    }
  }
  // Release anything leaked by the gpu-core client.
  fbl::AutoLock lock(&bar_lock_);
  // Start at 1, because we treat bar 0 specially.
  for (unsigned i = 1; i < PCI_MAX_BAR_COUNT; i++) {
    if (mapped_bars_[i].count) {
      zxlogf(WARNING, "Leaked bar %d", i);
      mapped_bars_[i].count = 1;
      IntelGpuCoreUnmapPciMmio(i);
    }
  }

  // bar 0 should have at most one ref left, otherwise log a leak like above and correct it.
  // We will leave it with one ref, because mmio_space_ will unmap it on destruction, and
  // we may need to access mmio_space_ while destroying member variables.
  if (mapped_bars_[0].count != mmio_space_.has_value()) {
    zxlogf(WARNING, "Leaked bar 0");
    if (mapped_bars_[0].count > 0) {
      mapped_bars_[0].count = 1;
    }
  }
}

// static
zx_status_t Controller::Create(zx_device_t* parent) {
  fbl::AllocChecker ac;
  std::unique_ptr<i915_tgl::Controller> dev(new (&ac) i915_tgl::Controller(parent));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = dev->Init();
  if (status == ZX_OK) {
    // devmgr now owns the memory for |dev|.
    dev.release();
  }

  return status;
}

}  // namespace i915_tgl

namespace {

constexpr zx_driver_ops_t kDriverOps = {
    .version = DRIVER_OPS_VERSION,
    .bind = [](void* ctx, zx_device_t* parent) { return i915_tgl::Controller::Create(parent); },
};

}  // namespace

ZIRCON_DRIVER(intel_i915, kDriverOps, "zircon", "0.1");
