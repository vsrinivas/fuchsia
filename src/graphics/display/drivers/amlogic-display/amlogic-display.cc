// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/amlogic-display/amlogic-display.h"

#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <fuchsia/hardware/amlogiccanvas/cpp/banjo.h>
#include <fuchsia/hardware/display/capture/cpp/banjo.h>
#include <fuchsia/hardware/display/clamprgb/cpp/banjo.h>
#include <fuchsia/hardware/display/controller/cpp/banjo.h>
#include <fuchsia/hardware/dsiimpl/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fit/defer.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/image-format-llcpp/image-format-llcpp.h>
#include <lib/image-format/image_format.h>
#include <lib/zircon-internal/align.h>
#include <lib/zx/channel.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/pixelformat.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <cstddef>
#include <iterator>

#include <ddk/metadata/display.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/vector.h>

#include "src/graphics/display/drivers/amlogic-display/amlogic-display-bind.h"
#include "src/graphics/display/drivers/amlogic-display/common.h"
#include "src/graphics/display/drivers/amlogic-display/vpp-regs.h"

namespace sysmem = fuchsia_sysmem;

namespace amlogic_display {

namespace {
constexpr uint32_t kCanvasLittleEndian64Bit = 7;
constexpr uint32_t kBufferAlignment = 64;

void SetDefaultImageFormatConstraints(sysmem::wire::PixelFormatType format, uint64_t modifier,
                                      sysmem::wire::ImageFormatConstraints& constraints) {
  constraints.color_spaces_count = 1;
  constraints.color_space[0].type = sysmem::wire::ColorSpaceType::kSrgb;
  constraints.pixel_format = {
      .type = format,
      .has_format_modifier = true,
      .format_modifier =
          {
              .value = modifier,
          },
  };
  constraints.bytes_per_row_divisor = kBufferAlignment;
  constraints.start_offset_divisor = kBufferAlignment;
}

}  // namespace

zx_status_t AmlogicDisplay::DisplayClampRgbImplSetMinimumRgb(uint8_t minimum_rgb) {
  if (fully_initialized()) {
    osd_->SetMinimumRgb(minimum_rgb);
    return ZX_OK;
  }
  return ZX_ERR_INTERNAL;
}

zx_status_t AmlogicDisplay::RestartDisplay() {
  if (!fully_initialized()) {
    return ZX_ERR_INTERNAL;
  }
  vpu_->PowerOff();
  vpu_->PowerOn();
  vpu_->VppInit();
  // Need to call this function since VPU/VPP registers were reset
  vpu_->SetFirstTimeDriverLoad();

  return vout_->RestartDisplay();
}

zx_status_t AmlogicDisplay::DisplayInit() {
  ZX_ASSERT(!fully_initialized());
  zx_status_t status;
  fbl::AllocChecker ac;

  // Setup VPU and VPP units first
  vpu_ = fbl::make_unique_checked<amlogic_display::Vpu>(&ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  status = vpu_->Init(pdev_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not initialize VPU object\n");
    return status;
  }

  // Determine whether it's first time boot or not
  const bool skip_disp_init = vpu_->SetFirstTimeDriverLoad();
  if (skip_disp_init) {
    DISP_INFO("First time driver load. Skip display initialization\n");
    // Make sure AFBC engine is on. Since bootloader does not use AFBC, it might not have powered
    // on AFBC engine.
    vpu_->AfbcPower(true);
  } else {
    DISP_INFO("Display driver reloaded. Initialize display system\n");
    RestartDisplay();
  }

  root_node_ = inspector_.GetRoot().CreateChild("amlogic-display");
  auto osd_or_status = amlogic_display::Osd::Create(
      &pdev_, vout_->supports_afbc(), vout_->fb_width(), vout_->fb_height(), vout_->display_width(),
      vout_->display_height(), &root_node_);
  if (osd_or_status.is_error()) {
    return osd_or_status.status_value();
  }
  osd_ = std::move(osd_or_status).value();

  osd_->HwInit();
  current_image_valid_ = false;
  return ZX_OK;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
void AmlogicDisplay::DisplayControllerImplSetDisplayControllerInterface(
    const display_controller_interface_protocol_t* intf) {
  fbl::AutoLock lock(&display_lock_);
  dc_intf_ = ddk::DisplayControllerInterfaceProtocolClient(intf);
  added_display_args_t args;
  vout_->PopulateAddedDisplayArgs(&args, display_id_);
  dc_intf_.OnDisplaysChanged(&args, 1, nullptr, 0, nullptr, 0, nullptr);
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t AmlogicDisplay::DisplayControllerImplImportImage(image_t* image,
                                                             zx_unowned_handle_t handle,
                                                             uint32_t index) {
  zx_status_t status = ZX_OK;
  auto import_info = std::make_unique<ImageInfo>();
  if (import_info == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  if (image->type != IMAGE_TYPE_SIMPLE || !format_support_check_(image->pixel_format)) {
    status = ZX_ERR_INVALID_ARGS;
    return status;
  }

  auto result = fidl::WireCall<sysmem::BufferCollection>(zx::unowned_channel(handle))
                    ->WaitForBuffersAllocated();
  if (!result.ok()) {
    return result.status();
  }
  if (result.value().status != ZX_OK) {
    return result.value().status;
  }

  sysmem::wire::BufferCollectionInfo2& collection_info = result.value().buffer_collection_info;

  if (!collection_info.settings.has_image_format_constraints ||
      index >= collection_info.buffer_count) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  ZX_DEBUG_ASSERT(
      collection_info.settings.image_format_constraints.pixel_format.has_format_modifier);

  const auto format_modifier =
      collection_info.settings.image_format_constraints.pixel_format.format_modifier.value;

  switch (format_modifier) {
    case sysmem::wire::kFormatModifierArmAfbc16X16:
    case sysmem::wire::kFormatModifierArmAfbc16X16Te: {
      // AFBC does not use canvas.
      uint64_t offset = collection_info.buffers[index].vmo_usable_start;
      size_t size =
          ZX_ROUNDUP(ImageFormatImageSize(image_format::ConstraintsToFormat(
                                              collection_info.settings.image_format_constraints,
                                              image->width, image->height)
                                              .value()),
                     PAGE_SIZE);
      zx_paddr_t paddr;
      zx_status_t status =
          bti_.pin(ZX_BTI_PERM_READ | ZX_BTI_CONTIGUOUS, collection_info.buffers[index].vmo,
                   offset & ~(PAGE_SIZE - 1), size, &paddr, 1, &import_info->pmt);
      if (status != ZX_OK) {
        DISP_ERROR("Could not pin BTI (%d)\n", status);
        return status;
      }
      import_info->paddr = paddr;
      import_info->image_height = image->height;
      import_info->image_width = image->width;
      import_info->is_afbc = true;
    } break;
    case sysmem::wire::kFormatModifierLinear:
    case sysmem::wire::kFormatModifierArmLinearTe: {
      uint32_t minimum_row_bytes;
      if (!image_format::GetMinimumRowBytes(collection_info.settings.image_format_constraints,
                                            image->width, &minimum_row_bytes)) {
        DISP_ERROR("Invalid image width %d for collection\n", image->width);
        return ZX_ERR_INVALID_ARGS;
      }
      canvas_info_t canvas_info;
      canvas_info.height = image->height;
      canvas_info.stride_bytes = minimum_row_bytes;
      canvas_info.wrap = 0;
      canvas_info.blkmode = 0;
      canvas_info.endianness = 0;
      canvas_info.flags = CANVAS_FLAGS_READ;

      uint8_t local_canvas_idx;
      status = canvas_.Config(std::move(collection_info.buffers[index].vmo),
                              collection_info.buffers[index].vmo_usable_start, &canvas_info,
                              &local_canvas_idx);
      if (status != ZX_OK) {
        DISP_ERROR("Could not configure canvas: %d\n", status);
        return ZX_ERR_NO_RESOURCES;
      }
      import_info->canvas = canvas_;
      import_info->canvas_idx = local_canvas_idx;
      import_info->image_height = image->height;
      import_info->image_width = image->width;
      import_info->is_afbc = false;
    } break;
    default:
      ZX_DEBUG_ASSERT_MSG(false, "Invalid pixel format modifier: %lu\n", format_modifier);
      return ZX_ERR_INVALID_ARGS;
  }
  image->handle = reinterpret_cast<uint64_t>(import_info.get());
  fbl::AutoLock lock(&image_lock_);
  imported_images_.push_back(std::move(import_info));
  return status;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
void AmlogicDisplay::DisplayControllerImplReleaseImage(image_t* image) {
  fbl::AutoLock lock(&image_lock_);
  auto info = reinterpret_cast<ImageInfo*>(image->handle);
  imported_images_.erase(*info);
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
uint32_t AmlogicDisplay::DisplayControllerImplCheckConfiguration(
    const display_config_t** display_configs, size_t display_count, uint32_t** layer_cfg_results,
    size_t* layer_cfg_result_count) {
  if (display_count != 1) {
    ZX_DEBUG_ASSERT(display_count == 0);
    return CONFIG_DISPLAY_OK;
  }

  fbl::AutoLock lock(&display_lock_);

  // no-op, just wait for the client to try a new config
  if (!display_attached_ || display_configs[0]->display_id != display_id_) {
    return CONFIG_DISPLAY_OK;
  }

  if (vout_->CheckMode(&display_configs[0]->mode)) {
    return CONFIG_DISPLAY_UNSUPPORTED_MODES;
  }

  bool success = true;

  if (display_configs[0]->layer_count > 1) {
    // We only support 1 layer
    success = false;
  }

  if (success && display_configs[0]->cc_flags) {
    // Make sure cc values are correct
    if (display_configs[0]->cc_flags & COLOR_CONVERSION_PREOFFSET) {
      for (int i = 0; i < 3; i++) {
        success = success && display_configs[0]->cc_preoffsets[i] > -1;
        success = success && display_configs[0]->cc_preoffsets[i] < 1;
      }
    }
    if (success && display_configs[0]->cc_flags & COLOR_CONVERSION_POSTOFFSET) {
      for (int i = 0; i < 3; i++) {
        success = success && display_configs[0]->cc_postoffsets[i] > -1;
        success = success && display_configs[0]->cc_postoffsets[i] < 1;
      }
    }
  }

  if (success && display_configs[0]->gamma_table_present) {
    // Make sure all channels have the same size and equal to the expected table size of hardware
    if (display_configs[0]->gamma_red_count != Osd::kGammaTableSize ||
        display_configs[0]->gamma_red_count != display_configs[0]->gamma_green_count ||
        display_configs[0]->gamma_red_count != display_configs[0]->gamma_blue_count) {
      layer_cfg_results[0][0] |= CLIENT_GAMMA;
    }
  }

  if (success) {
    const uint32_t width = display_configs[0]->mode.h_addressable;
    const uint32_t height = display_configs[0]->mode.v_addressable;
    // Make sure ther layer configuration is supported
    const primary_layer_t& layer = display_configs[0]->layer_list[0]->cfg.primary;
    frame_t frame = {
        .x_pos = 0,
        .y_pos = 0,
        .width = width,
        .height = height,
    };

    if (layer.alpha_mode == ALPHA_PREMULTIPLIED) {
      // we don't support pre-multiplied alpha mode
      layer_cfg_results[0][0] |= CLIENT_ALPHA;
    }
    success = display_configs[0]->layer_list[0]->type == LAYER_TYPE_PRIMARY &&
              layer.transform_mode == FRAME_TRANSFORM_IDENTITY && layer.image.width == width &&
              layer.image.height == height &&
              memcmp(&layer.dest_frame, &frame, sizeof(frame_t)) == 0 &&
              memcmp(&layer.src_frame, &frame, sizeof(frame_t)) == 0;
  }
  if (!success) {
    layer_cfg_results[0][0] = CLIENT_MERGE_BASE;
    for (unsigned i = 1; i < display_configs[0]->layer_count; i++) {
      layer_cfg_results[0][i] = CLIENT_MERGE_SRC;
    }
  }
  return CONFIG_DISPLAY_OK;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
void AmlogicDisplay::DisplayControllerImplApplyConfiguration(
    const display_config_t** display_configs, size_t display_count,
    const config_stamp_t* config_stamp) {
  ZX_DEBUG_ASSERT(display_configs);
  ZX_DEBUG_ASSERT(config_stamp);

  fbl::AutoLock lock(&display_lock_);

  zx_status_t status;
  if (display_count == 1 && display_configs[0]->layer_count) {
    // Setting up OSD may require Vout framebuffer information, which may be
    // changed on each ApplyConfiguration(), so we need to apply the
    // configuration to Vout first before initializing the display and OSD.
    status = vout_->ApplyConfiguration(&display_configs[0]->mode);
    if (status != ZX_OK) {
      DISP_ERROR("Could not apply config to Vout! %d\n", status);
      return;
    }

    if (!fully_initialized()) {
      if ((status = DisplayInit()) != ZX_OK) {
        DISP_ERROR("Display Hardware Initialization failed! %d\n", status);
        ZX_ASSERT(0);
      }
      set_fully_initialized();
    }

    // The only way a checked configuration could now be invalid is if display was
    // unplugged. If that's the case, then the upper layers will give a new configuration
    // once they finish handling the unplug event. So just return.
    if (!display_attached_ || display_configs[0]->display_id != display_id_) {
      return;
    }

    // Since Amlogic does not support plug'n play (fixed display), there is no way
    // a checked configuration could be invalid at this point.
    auto info =
        reinterpret_cast<ImageInfo*>(display_configs[0]->layer_list[0]->cfg.primary.image.handle);
    current_image_valid_ = true;
    current_image_ = display_configs[0]->layer_list[0]->cfg.primary.image.handle;
    osd_->FlipOnVsync(info->canvas_idx, display_configs[0], config_stamp);
  } else {
    current_image_valid_ = false;
    if (fully_initialized()) {
      {
        fbl::AutoLock lock2(&capture_lock_);
        if (capture_active_id_ != INVALID_ID) {
          // there's an active capture. stop it before disabling osd
          vpu_->CaptureDone();
          capture_active_id_ = INVALID_ID;
        }
      }
      osd_->Disable(*config_stamp);
    }
  }

  // If bootloader does not enable any of the display hardware, no vsync will be generated.
  // This fakes a vsync to let clients know we are ready until we actually initialize hardware
  if (!fully_initialized()) {
    if (dc_intf_.is_valid()) {
      if (display_count == 0 || display_configs[0]->layer_count == 0) {
        dc_intf_.OnDisplayVsync(display_id_, zx_clock_get_monotonic(), config_stamp);
      }
    }
  }
}

void AmlogicDisplay::DdkSuspend(ddk::SuspendTxn txn) {
  if (txn.suspend_reason() != DEVICE_SUSPEND_REASON_MEXEC) {
    txn.Reply(ZX_ERR_NOT_SUPPORTED, txn.requested_state());
    return;
  }
  if (fully_initialized()) {
    osd_->Disable();
  }

  fbl::AutoLock l(&image_lock_);
  for (auto& i : imported_images_) {
    if (i.pmt) {
      i.pmt.unpin();
    }
    if (i.canvas.is_valid() && i.canvas_idx > 0) {
      i.canvas.Free(i.canvas_idx);
    }
  }
  txn.Reply(ZX_OK, txn.requested_state());
}

void AmlogicDisplay::DdkResume(ddk::ResumeTxn txn) {
  if (fully_initialized()) {
    osd_->Enable();
  }
  txn.Reply(ZX_OK, DEV_POWER_STATE_D0, txn.requested_state());
}

void AmlogicDisplay::DdkRelease() {
  vsync_irq_.destroy();
  thrd_join(vsync_thread_, nullptr);
  if (fully_initialized()) {
    osd_->Release();
    vpu_->PowerOff();
  }

  vd1_wr_irq_.destroy();
  thrd_join(capture_thread_, nullptr);
  hpd_irq_.destroy();
  thrd_join(hpd_thread_, nullptr);
  delete this;
}

zx_status_t AmlogicDisplay::DdkGetProtocol(uint32_t proto_id, void* out_protocol) {
  auto* proto = static_cast<ddk::AnyProtocol*>(out_protocol);
  proto->ctx = this;
  switch (proto_id) {
    case ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL:
      proto->ops = &display_controller_impl_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_DISPLAY_CAPTURE_IMPL:
      if (!vout_->supports_capture()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      proto->ops = &display_capture_impl_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_DISPLAY_CLAMP_RGB_IMPL:
      proto->ops = &display_clamp_rgb_impl_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_I2C_IMPL:
      if (!vout_->supports_hpd()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      proto->ops = &i2c_impl_protocol_ops_;
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t AmlogicDisplay::SetupDisplayInterface() {
  fbl::AutoLock lock(&display_lock_);

  added_display_info_t info{.is_standard_srgb_out = 0};  // Random default
  if (dc_intf_.is_valid()) {
    added_display_args_t args;
    vout_->PopulateAddedDisplayArgs(&args, display_id_);
    dc_intf_.OnDisplaysChanged(&args, 1, nullptr, 0, &info, 1, nullptr);
  }

  return vout_->OnDisplaysChanged(info);
}

zx_status_t AmlogicDisplay::DisplayControllerImplGetSysmemConnection(zx::channel connection) {
  zx_status_t status = sysmem_.Connect(std::move(connection));
  if (status != ZX_OK) {
    DISP_ERROR("Could not connect to sysmem\n");
    return status;
  }

  return ZX_OK;
}

zx_status_t AmlogicDisplay::DisplayControllerImplSetBufferCollectionConstraints(
    const image_t* config, zx_unowned_handle_t collection) {
  sysmem::wire::BufferCollectionConstraints constraints = {};
  const char* buffer_name;
  if (config->type == IMAGE_TYPE_CAPTURE) {
    constraints.usage.cpu = sysmem::wire::kCpuUsageReadOften | sysmem::wire::kCpuUsageWriteOften;
  } else {
    constraints.usage.display = sysmem::wire::kDisplayUsageLayer;
  }
  constraints.has_buffer_memory_constraints = true;
  sysmem::wire::BufferMemoryConstraints& buffer_constraints = constraints.buffer_memory_constraints;
  buffer_constraints.physically_contiguous_required = true;
  buffer_constraints.secure_required = false;
  buffer_constraints.ram_domain_supported = true;
  buffer_constraints.cpu_domain_supported = false;
  buffer_constraints.inaccessible_domain_supported = true;
  buffer_constraints.heap_permitted_count = 2;
  buffer_constraints.heap_permitted[0] = sysmem::wire::HeapType::kSystemRam;
  buffer_constraints.heap_permitted[1] = sysmem::wire::HeapType::kAmlogicSecure;

  if (config->type == IMAGE_TYPE_CAPTURE) {
    constraints.image_format_constraints_count = 1;
    sysmem::wire::ImageFormatConstraints& image_constraints =
        constraints.image_format_constraints[0];

    SetDefaultImageFormatConstraints(sysmem::wire::PixelFormatType::kBgr24,
                                     sysmem::wire::kFormatModifierLinear, image_constraints);
    image_constraints.min_coded_width = vout_->display_width();
    image_constraints.max_coded_width = vout_->display_width();
    image_constraints.min_coded_height = vout_->display_height();
    image_constraints.max_coded_height = vout_->display_height();
    image_constraints.min_bytes_per_row = ZX_ALIGN(
        vout_->display_width() * ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_888), kBufferAlignment);
    image_constraints.max_coded_width_times_coded_height =
        vout_->display_width() * vout_->display_height();
    buffer_name = "Display capture";
  } else {
    // TODO(fxbug.dev/94535): Currently the buffer collection constraints are
    // applied to all displays. If the |vout_| device type changes, then the
    // existing image formats might not work for the new device type. To resolve
    // this, the driver should set per-display buffer collection constraints
    // instead.
    constraints.image_format_constraints_count = 0;
    ZX_DEBUG_ASSERT(format_support_check_ != nullptr);
    if (format_support_check_(ZX_PIXEL_FORMAT_RGB_x888) ||
        format_support_check_(ZX_PIXEL_FORMAT_ARGB_8888)) {
      for (const auto format_modifier :
           {sysmem::wire::kFormatModifierLinear, sysmem::wire::kFormatModifierArmLinearTe}) {
        const size_t index = constraints.image_format_constraints_count++;
        auto& image_constraints = constraints.image_format_constraints[index];
        SetDefaultImageFormatConstraints(sysmem::wire::PixelFormatType::kBgra32, format_modifier,
                                         image_constraints);
      }
    }
    if (format_support_check_(ZX_PIXEL_FORMAT_BGR_888x) ||
        format_support_check_(ZX_PIXEL_FORMAT_ABGR_8888)) {
      for (const auto format_modifier : {sysmem::wire::kFormatModifierArmAfbc16X16,
                                         sysmem::wire::kFormatModifierArmAfbc16X16Te}) {
        const size_t index = constraints.image_format_constraints_count++;
        auto& image_constraints = constraints.image_format_constraints[index];
        SetDefaultImageFormatConstraints(sysmem::wire::PixelFormatType::kR8G8B8A8, format_modifier,
                                         image_constraints);
      }
    }
    buffer_name = "Display";
  }

  // Set priority to 10 to override the Vulkan driver name priority of 5, but be less than most
  // application priorities.
  constexpr uint32_t kNamePriority = 10;
  auto name_res = fidl::WireCall<sysmem::BufferCollection>(zx::unowned_channel(collection))
                      ->SetName(kNamePriority, fidl::StringView::FromExternal(buffer_name));
  if (!name_res.ok()) {
    DISP_ERROR("Failed to set name: %d", name_res.status());
    return name_res.status();
  }
  auto res = fidl::WireCall<sysmem::BufferCollection>(zx::unowned_channel(collection))
                 ->SetConstraints(true, constraints);

  if (!res.ok()) {
    DISP_ERROR("Failed to set constraints: %d", res.status());
    return res.status();
  }

  return ZX_OK;
}

zx_status_t AmlogicDisplay::DisplayControllerImplSetDisplayPower(uint64_t display_id,
                                                                 bool power_on) {
  if (display_id != display_id_ || !display_attached_) {
    return ZX_ERR_NOT_FOUND;
  }
  if (power_on) {
    return vout_->PowerOn().status_value();
  } else {
    return vout_->PowerOff().status_value();
  }
}

void AmlogicDisplay::DisplayCaptureImplSetDisplayCaptureInterface(
    const display_capture_interface_protocol_t* intf) {
  fbl::AutoLock lock(&capture_lock_);
  capture_intf_ = ddk::DisplayCaptureInterfaceProtocolClient(intf);
  capture_active_id_ = INVALID_ID;
}

zx_status_t AmlogicDisplay::DisplayCaptureImplImportImageForCapture(zx_unowned_handle_t collection,
                                                                    uint32_t index,
                                                                    uint64_t* out_capture_handle) {
  auto import_capture = std::make_unique<ImageInfo>();
  if (import_capture == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  fbl::AutoLock lock(&capture_lock_);
  auto result = fidl::WireCall<sysmem::BufferCollection>(zx::unowned_channel(collection))
                    ->WaitForBuffersAllocated();
  if (!result.ok()) {
    return result.status();
  }
  if (result.value().status != ZX_OK) {
    return result.value().status;
  }

  sysmem::wire::BufferCollectionInfo2& collection_info = result.value().buffer_collection_info;

  if (!collection_info.settings.has_image_format_constraints ||
      index >= collection_info.buffer_count) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Ensure the proper format
  ZX_DEBUG_ASSERT(collection_info.settings.image_format_constraints.pixel_format.type ==
                  sysmem::wire::PixelFormatType::kBgr24);

  // Allocate a canvas for the capture image
  canvas_info_t canvas_info = {};
  canvas_info.height = collection_info.settings.image_format_constraints.min_coded_height;
  canvas_info.stride_bytes = collection_info.settings.image_format_constraints.min_bytes_per_row;
  canvas_info.wrap = 0;
  canvas_info.blkmode = 0;
  canvas_info.endianness = kCanvasLittleEndian64Bit;
  canvas_info.flags = CANVAS_FLAGS_READ | CANVAS_FLAGS_WRITE;
  uint8_t canvas_idx;
  zx_status_t status =
      canvas_.Config(std::move(collection_info.buffers[index].vmo),
                     collection_info.buffers[index].vmo_usable_start, &canvas_info, &canvas_idx);
  if (status != ZX_OK) {
    DISP_ERROR("Could not configure canvas %d\n", status);
    return status;
  }

  // At this point, we have setup a canvas with the BufferCollection-based VMO. Store the
  // capture information
  import_capture->canvas_idx = canvas_idx;
  import_capture->canvas = canvas_;
  import_capture->image_height = collection_info.settings.image_format_constraints.min_coded_height;
  import_capture->image_width = collection_info.settings.image_format_constraints.min_coded_width;
  *out_capture_handle = reinterpret_cast<uint64_t>(import_capture.get());
  imported_captures_.push_back(std::move(import_capture));
  return ZX_OK;
}

zx_status_t AmlogicDisplay::DisplayCaptureImplStartCapture(uint64_t capture_handle) {
  if (!fully_initialized()) {
    DISP_ERROR("Cannot start capture before initializing the display\n");
    return ZX_ERR_SHOULD_WAIT;
  }

  fbl::AutoLock lock(&capture_lock_);
  if (capture_active_id_ != INVALID_ID) {
    DISP_ERROR("Cannot start capture while another capture is in progress\n");
    return ZX_ERR_SHOULD_WAIT;
  }

  // Confirm a valid image is being displayed
  // Check whether a valid image is being displayed at the time of start capture.
  // There is a chance that a client might release the image being displayed during
  // capture, but that behavior is not within specified spec
  {
    fbl::AutoLock lock2(&display_lock_);
    if (!current_image_valid_) {
      DISP_ERROR("No Valid Image is being displayed\n");
      return ZX_ERR_UNAVAILABLE;
    }
  }

  // Confirm that the handle was previously imported (hence valid)
  auto info = reinterpret_cast<ImageInfo*>(capture_handle);
  if (imported_captures_.find_if([info](auto& i) { return i.canvas_idx == info->canvas_idx; }) ==
      imported_captures_.end()) {
    // invalid handle
    DISP_ERROR("Invalid capture_handle\n");
    return ZX_ERR_NOT_FOUND;
  }

  ZX_DEBUG_ASSERT(info->canvas_idx > 0);
  ZX_DEBUG_ASSERT(info->image_height > 0);
  ZX_DEBUG_ASSERT(info->image_width > 0);

  auto status = vpu_->CaptureInit(info->canvas_idx, info->image_height, info->image_width);
  if (status != ZX_OK) {
    DISP_ERROR("Failed to init capture %d\n", status);
    return status;
  }

  status = vpu_->CaptureStart();
  if (status != ZX_OK) {
    DISP_ERROR("Failed to start capture %d\n", status);
    return status;
  }
  capture_active_id_ = capture_handle;
  return ZX_OK;
}

zx_status_t AmlogicDisplay::DisplayCaptureImplReleaseCapture(uint64_t capture_handle) {
  fbl::AutoLock lock(&capture_lock_);
  if (capture_handle == capture_active_id_) {
    return ZX_ERR_SHOULD_WAIT;
  }

  // Find and erase previously imported capture
  auto idx = reinterpret_cast<ImageInfo*>(capture_handle)->canvas_idx;
  if (imported_captures_.erase_if([idx](auto& i) { return i.canvas_idx == idx; }) == nullptr) {
    DISP_ERROR("Tried to release non-existent capture image %d\n", idx);
    return ZX_ERR_NOT_FOUND;
  }

  return ZX_OK;
}

bool AmlogicDisplay::DisplayCaptureImplIsCaptureCompleted() {
  fbl::AutoLock lock(&capture_lock_);
  return (capture_active_id_ == INVALID_ID);
}

int AmlogicDisplay::CaptureThread() {
  zx_status_t status;
  while (true) {
    zx::time timestamp;
    status = vd1_wr_irq_.wait(&timestamp);
    if (status != ZX_OK) {
      DISP_ERROR("Vd1 Wr interrupt wait failed %d\n", status);
      break;
    }
    if (!fully_initialized()) {
      DISP_ERROR("Capture interrupt fired before the display was initialized\n");
      continue;
    }
    vpu_->CaptureDone();
    fbl::AutoLock lock(&capture_lock_);
    if (capture_intf_.is_valid()) {
      capture_intf_.OnCaptureComplete();
    }
    capture_active_id_ = INVALID_ID;
  }
  return status;
}

int AmlogicDisplay::VSyncThread() {
  zx_status_t status;
  while (true) {
    zx::time timestamp;
    status = vsync_irq_.wait(&timestamp);
    if (status != ZX_OK) {
      DISP_ERROR("VSync Interrupt Wait failed\n");
      break;
    }
    std::optional<config_stamp_t> current_config_stamp = std::nullopt;
    if (fully_initialized()) {
      current_config_stamp = osd_->GetLastConfigStampApplied();
    }
    fbl::AutoLock lock(&display_lock_);
    if (dc_intf_.is_valid() && display_attached_) {
      dc_intf_.OnDisplayVsync(display_id_, timestamp.get(),
                              current_config_stamp.has_value() ? &*current_config_stamp : nullptr);
    }
  }

  return status;
}

int AmlogicDisplay::HpdThread() {
  zx_status_t status;
  while (1) {
    status = hpd_irq_.wait(NULL);
    if (status != ZX_OK) {
      DISP_ERROR("Waiting in Interrupt failed %d\n", status);
      break;
    }
    usleep(500000);
    uint8_t hpd;
    status = hpd_gpio_.Read(&hpd);
    if (status != ZX_OK) {
      DISP_ERROR("gpio_read failed HDMI HPD\n");
      continue;
    }

    fbl::AutoLock lock(&display_lock_);

    bool display_added = false;
    added_display_args_t args;
    added_display_info_t info;
    uint64_t display_removed = INVALID_DISPLAY_ID;
    if (hpd && !display_attached_) {
      DISP_ERROR("Display is connected\n");

      display_attached_ = true;
      vout_->DisplayConnected();
      vout_->PopulateAddedDisplayArgs(&args, display_id_);
      display_added = true;
      hpd_gpio_.SetPolarity(GPIO_POLARITY_LOW);
    } else if (!hpd && display_attached_) {
      DISP_ERROR("Display Disconnected!\n");
      vout_->DisplayDisconnected();

      display_removed = display_id_;
      display_id_++;
      display_attached_ = false;

      hpd_gpio_.SetPolarity(GPIO_POLARITY_HIGH);
    }

    if (dc_intf_.is_valid() && (display_removed != INVALID_DISPLAY_ID || display_added)) {
      dc_intf_.OnDisplaysChanged(&args, display_added ? 1 : 0, &display_removed,
                                 display_removed != INVALID_DISPLAY_ID, &info,
                                 display_added ? 1 : 0, NULL);
      if (display_added) {
        // See if we need to change output color to RGB
        status = vout_->OnDisplaysChanged(info);
      }
    }
  }
  return status;
}

// TODO(payamm): make sure unbind/release are called if we return error
zx_status_t AmlogicDisplay::Bind() {
  fbl::AllocChecker ac;
  vout_ = fbl::make_unique_checked<amlogic_display::Vout>(&ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  SetFormatSupportCheck([this](auto format) { return vout_->IsFormatSupported(format); });

  display_panel_t display_info;
  size_t actual;
  zx_status_t status = device_get_metadata(parent_, DEVICE_METADATA_DISPLAY_CONFIG, &display_info,
                                           sizeof(display_info), &actual);
  if (status != ZX_OK) {
    status = vout_->InitHdmi(parent_);
    if (status != ZX_OK) {
      DISP_ERROR("Could not initialize HDMI Vout device! %d\n", status);
      return status;
    }
  } else if (actual != sizeof(display_panel_t)) {
    DISP_ERROR("Could not get display panel metadata %d\n", status);
    return status;
  } else {
    DISP_INFO("Provided Display Info: %d x %d with panel type %d\n", display_info.width,
              display_info.height, display_info.panel_type);
    display_attached_ = true;

    fbl::AutoLock lock(&display_lock_);
    status =
        vout_->InitDsi(parent_, display_info.panel_type, display_info.width, display_info.height);
    if (status != ZX_OK) {
      DISP_ERROR("Could not initialize DSI Vout device! %d\n", status);
      return status;
    }
  }

  status = ddk::PDev::FromFragment(parent_, &pdev_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get PDEV protocol\n");
    return status;
  }

  // Get board info
  status = pdev_.GetBoardInfo(&board_info_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not obtain board info\n");
    return status;
  }

  status = ddk::SysmemProtocolClient::CreateFromDevice(parent_, "sysmem", &sysmem_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get Display SYSMEM protocol\n");
    return status;
  }

  status = ddk::AmlogicCanvasProtocolClient::CreateFromDevice(parent_, "canvas", &canvas_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not obtain CANVAS protocol\n");
    return status;
  }

  status = pdev_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get BTI handle\n");
    return status;
  }

  // Setup Display Interface
  status = SetupDisplayInterface();
  if (status != ZX_OK) {
    DISP_ERROR("Amlogic display setup failed! %d\n", status);
    return status;
  }

  // Map VSync Interrupt
  status = pdev_.GetInterrupt(IRQ_VSYNC, 0, &vsync_irq_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map vsync interrupt\n");
    return status;
  }

  auto start_thread = [](void* arg) { return static_cast<AmlogicDisplay*>(arg)->VSyncThread(); };
  status = thrd_create_with_name(&vsync_thread_, start_thread, this, "vsync_thread");
  if (status != ZX_OK) {
    DISP_ERROR("Could not create vsync_thread\n");
    return status;
  }

  if (vout_->supports_capture()) {
    // Map VD1_WR Interrupt (used for capture)
    status = pdev_.GetInterrupt(IRQ_VD1_WR, 0, &vd1_wr_irq_);
    if (status != ZX_OK) {
      DISP_ERROR("Could not map vd1 wr interrupt\n");
      return status;
    }

    auto vd_thread = [](void* arg) { return static_cast<AmlogicDisplay*>(arg)->CaptureThread(); };
    status = thrd_create_with_name(&capture_thread_, vd_thread, this, "capture_thread");
    if (status != ZX_OK) {
      DISP_ERROR("Could not create capture_thread\n");
      return status;
    }
  }

  if (vout_->supports_hpd()) {
    status = ddk::GpioProtocolClient::CreateFromDevice(parent_, "gpio", &hpd_gpio_);
    if (status != ZX_OK) {
      DISP_ERROR("Could not obtain GPIO protocol\n");
      return status;
    }

    status = hpd_gpio_.ConfigIn(GPIO_PULL_DOWN);
    if (status != ZX_OK) {
      DISP_ERROR("gpio_config_in failed for gpio\n");
      return status;
    }

    status = hpd_gpio_.GetInterrupt(ZX_INTERRUPT_MODE_LEVEL_HIGH, &hpd_irq_);
    if (status != ZX_OK) {
      DISP_ERROR("gpio_get_interrupt failed for gpio\n");
      return status;
    }

    auto hpd_thread = [](void* arg) { return static_cast<AmlogicDisplay*>(arg)->HpdThread(); };
    status = thrd_create_with_name(&hpd_thread_, hpd_thread, this, "hpd_thread");
    if (status != ZX_OK) {
      DISP_ERROR("Could not create hpd_thread\n");
      return status;
    }
  }

  // Set profile for vsync thread.
  // TODO(fxbug.dev/40858): Migrate to the role-based API when available, instead of hard
  // coding parameters.
  {
    const zx_duration_t capacity = ZX_USEC(500);
    const zx_duration_t deadline = ZX_MSEC(8);
    const zx_duration_t period = deadline;

    zx_handle_t profile = ZX_HANDLE_INVALID;
    if ((status = device_get_deadline_profile(this->zxdev(), capacity, deadline, period,
                                              "dev/display/amlogic-display/vsync_thread",
                                              &profile)) != ZX_OK) {
      DISP_ERROR("Failed to get deadline profile: %d\n", status);
    } else {
      const zx_handle_t thread_handle = thrd_get_zx_handle(vsync_thread_);
      status = zx_object_set_profile(thread_handle, profile, 0);
      if (status != ZX_OK) {
        DISP_ERROR("Failed to set deadline profile: %d\n", status);
      }
      zx_handle_close(profile);
    }
  }

  auto cleanup = fit::defer([&]() { DdkRelease(); });

  status = DdkAdd(ddk::DeviceAddArgs("amlogic-display")
                      .set_flags(DEVICE_ADD_ALLOW_MULTI_COMPOSITE)
                      .set_inspect_vmo(inspector_.DuplicateVmo()));
  if (status != ZX_OK) {
    DISP_ERROR("Could not add device\n");
    return status;
  }

  cleanup.cancel();

  return ZX_OK;
}

// main bind function called from dev manager
zx_status_t amlogic_display_bind(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<amlogic_display::AmlogicDisplay>(&ac, parent);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

static constexpr zx_driver_ops_t amlogic_display_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = amlogic_display_bind;
  return ops;
}();

}  // namespace amlogic_display

// clang-format off
ZIRCON_DRIVER(amlogic_display, amlogic_display::amlogic_display_ops, "zircon", "0.1");
