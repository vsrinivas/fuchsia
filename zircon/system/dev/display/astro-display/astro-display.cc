// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "astro-display.h"

#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/image-format-llcpp/image-format-llcpp.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/amlogiccanvas.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/display/controller.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/protocol/display/capture.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/vector.h>

#include "common.h"
#include "lib/zx/channel.h"
#include "zircon/pixelformat.h"

namespace sysmem = llcpp::fuchsia::sysmem;

namespace astro_display {

namespace {

// List of supported pixel formats
zx_pixel_format_t kSupportedPixelFormats[] = {ZX_PIXEL_FORMAT_RGB_x888};

constexpr uint64_t kDisplayId = PANEL_DISPLAY_ID;

// Astro/Sherlock Display Configuration. These configuration comes directly from
// from LCD vendor and hardware team.
constexpr display_setting_t kDisplaySettingTV070WSM_FT = {
    .lane_num = 4,
    .bit_rate_max = 360,
    .clock_factor = 8,
    .lcd_clock = 44250000,
    .h_active = 600,
    .v_active = 1024,
    .h_period = 700,
    .v_period = 1053,
    .hsync_width = 24,
    .hsync_bp = 36,
    .hsync_pol = 0,
    .vsync_width = 2,
    .vsync_bp = 8,
    .vsync_pol = 0,
};
constexpr display_setting_t kDisplaySettingP070ACB_FT = {
    .lane_num = 4,
    .bit_rate_max = 400,
    .clock_factor = 8,
    .lcd_clock = 49434000,
    .h_active = 600,
    .v_active = 1024,
    .h_period = 770,
    .v_period = 1070,
    .hsync_width = 10,
    .hsync_bp = 80,
    .hsync_pol = 0,
    .vsync_width = 6,
    .vsync_bp = 20,
    .vsync_pol = 0,
};
constexpr display_setting_t kDisplaySettingG101B158_FT = {
    .lane_num = 4,
    .bit_rate_max = 566,
    .clock_factor = 8,
    .lcd_clock = 70701600,
    .h_active = 800,
    .v_active = 1280,
    .h_period = 890,
    .v_period = 1324,
    .hsync_width = 24,
    .hsync_bp = 20,
    .hsync_pol = 0,
    .vsync_width = 4,
    .vsync_bp = 20,
    .vsync_pol = 0,
};
constexpr display_setting_t kDisplaySettingTV101WXM_FT = {
    .lane_num = 4,
    .bit_rate_max = 566,
    .clock_factor = 8,
    .lcd_clock = 70701600,
    .h_active = 800,
    .v_active = 1280,
    .h_period = 890,
    .v_period = 1324,
    .hsync_width = 20,
    .hsync_bp = 50,
    .hsync_pol = 0,
    .vsync_width = 4,
    .vsync_bp = 20,
    .vsync_pol = 0,
};

constexpr uint32_t kCanvasLittleEndian64Bit = 7;
constexpr uint32_t kBufferAlignment = 64;
}  // namespace

// This function copies the display settings into our internal structure
void AstroDisplay::CopyDisplaySettings() {
  ZX_DEBUG_ASSERT(init_disp_table_);

  disp_setting_.h_active = init_disp_table_->h_active;
  disp_setting_.v_active = init_disp_table_->v_active;
  disp_setting_.h_period = init_disp_table_->h_period;
  disp_setting_.v_period = init_disp_table_->v_period;
  disp_setting_.hsync_width = init_disp_table_->hsync_width;
  disp_setting_.hsync_bp = init_disp_table_->hsync_bp;
  disp_setting_.hsync_pol = init_disp_table_->hsync_pol;
  disp_setting_.vsync_width = init_disp_table_->vsync_width;
  disp_setting_.vsync_bp = init_disp_table_->vsync_bp;
  disp_setting_.vsync_pol = init_disp_table_->vsync_pol;
  disp_setting_.lcd_clock = init_disp_table_->lcd_clock;
  disp_setting_.clock_factor = init_disp_table_->clock_factor;
  disp_setting_.lane_num = init_disp_table_->lane_num;
  disp_setting_.bit_rate_max = init_disp_table_->bit_rate_max;
}

void AstroDisplay::PopulateAddedDisplayArgs(added_display_args_t* args) {
  args->display_id = kDisplayId;
  args->edid_present = false;
  args->panel.params.height = height_;
  args->panel.params.width = width_;
  args->panel.params.refresh_rate_e2 = 6000;  // Just guess that it's 60fps
  args->pixel_format_list = kSupportedPixelFormats;
  args->pixel_format_count = countof(kSupportedPixelFormats);
  args->cursor_info_count = 0;
}

zx_status_t AstroDisplay::DisplayInit() {
  zx_status_t status;
  fbl::AllocChecker ac;

  // Setup VPU and VPP units first
  vpu_ = fbl::make_unique_checked<astro_display::Vpu>(&ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  status = vpu_->Init(components_[COMPONENT_PDEV]);
  if (status != ZX_OK) {
    DISP_ERROR("Could not initialize VPU object\n");
    return status;
  }

  // Determine whether it's first time boot or not
  bool skip_disp_init = false;
  if (vpu_->SetFirstTimeDriverLoad()) {
    DISP_INFO("First time driver load. Skip display initialization\n");
    skip_disp_init = true;
  } else {
    skip_disp_init = false;
    DISP_INFO("Display driver reloaded. Initialize display system\n");
  }

  // Detect panel type
  PopulatePanelType();
  if (panel_type_ == PANEL_TV070WSM_FT) {
    init_disp_table_ = &kDisplaySettingTV070WSM_FT;
  } else if (panel_type_ == PANEL_P070ACB_FT) {
    init_disp_table_ = &kDisplaySettingP070ACB_FT;
  } else if (panel_type_ == PANEL_TV101WXM_FT) {
    init_disp_table_ = &kDisplaySettingTV101WXM_FT;
  } else if (panel_type_ == PANEL_G101B158_FT) {
    init_disp_table_ = &kDisplaySettingG101B158_FT;
  } else {
    DISP_ERROR("Unsupported panel detected!\n");
    status = ZX_ERR_NOT_SUPPORTED;
    return status;
  }

  // Populated internal structures based on predefined tables
  CopyDisplaySettings();

  // Ensure Max Bit Rate / pixel clock ~= 8 (8.xxx). This is because the clock calculation
  // part of code assumes a clock factor of 1. All the LCD tables from Astro have this
  // relationship established. We'll have to revisit the calculation if this ratio cannot
  // be met.
  if (init_disp_table_->bit_rate_max / (init_disp_table_->lcd_clock / 1000 / 1000) != 8) {
    DISP_ERROR("Max Bit Rate / pixel clock != 8\n");
    status = ZX_ERR_INVALID_ARGS;
    return status;
  }

  if (!skip_disp_init) {
    vpu_->PowerOff();
    vpu_->PowerOn();
    vpu_->VppInit();
    // Need to call this function since VPU/VPP registers were reset
    vpu_->SetFirstTimeDriverLoad();
    clock_ = fbl::make_unique_checked<astro_display::AstroDisplayClock>(&ac);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    status = clock_->Init(components_[COMPONENT_PDEV]);
    if (status != ZX_OK) {
      DISP_ERROR("Could not initialize Clock object\n");
      return status;
    }

    // Enable all display related clocks
    status = clock_->Enable(disp_setting_);
    if (status != ZX_OK) {
      DISP_ERROR("Could not enable display clocks!\n");
      return status;
    }

    // Program and Enable DSI Host Interface
    dsi_host_ = fbl::make_unique_checked<astro_display::AmlDsiHost>(
        &ac, components_[COMPONENT_PDEV], components_[COMPONENT_DSI],
        components_[COMPONENT_LCD_GPIO], clock_->GetBitrate(), panel_type_);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    status = dsi_host_->Init();
    if (status != ZX_OK) {
      DISP_ERROR("Could not initialize DSI Host\n");
      return status;
    }

    status = dsi_host_->HostOn(disp_setting_);
    if (status != ZX_OK) {
      DISP_ERROR("DSI Host On failed! %d\n", status);
      return status;
    }
  }
  osd_ = fbl::make_unique_checked<astro_display::Osd>(&ac, width_, height_, disp_setting_.h_active,
                                                      disp_setting_.v_active);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // Initialize osd object
  status = osd_->Init(components_[COMPONENT_PDEV]);
  if (status != ZX_OK) {
    DISP_ERROR("Could not initialize OSD object\n");
    return status;
  }

  osd_->HwInit();

  // Configure osd layer
  current_image_valid_ = false;
  status = osd_->Configure();
  if (status != ZX_OK) {
    DISP_ERROR("OSD configuration failed!\n");
    return status;
  }

  return ZX_OK;
}

static uint32_t ComputeLinearStride(uint32_t width, zx_pixel_format_t format) {
  // The astro display controller needs buffers with a stride that is an even
  // multiple of 32.
  return ROUNDUP(width, 32 / ZX_PIXEL_FORMAT_BYTES(format));
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
void AstroDisplay::DisplayControllerImplSetDisplayControllerInterface(
    const display_controller_interface_protocol_t* intf) {
  fbl::AutoLock lock(&display_lock_);
  dc_intf_ = ddk::DisplayControllerInterfaceProtocolClient(intf);
  added_display_args_t args;
  PopulateAddedDisplayArgs(&args);
  dc_intf_.OnDisplaysChanged(&args, 1, nullptr, 0, nullptr, 0, nullptr);
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t AstroDisplay::DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo,
                                                              size_t offset) {
  return ZX_ERR_NOT_SUPPORTED;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t AstroDisplay::DisplayControllerImplImportImage(image_t* image,
                                                           zx_unowned_handle_t handle,
                                                           uint32_t index) {
  zx_status_t status = ZX_OK;
  auto import_info = std::make_unique<ImageInfo>();
  if (import_info == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  if (image->type != IMAGE_TYPE_SIMPLE || image->pixel_format != format_) {
    status = ZX_ERR_INVALID_ARGS;
    return status;
  }

  auto result =
      sysmem::BufferCollection::Call::WaitForBuffersAllocated(zx::unowned_channel(handle));
  if (!result.ok()) {
    return result.status();
  }
  if (result->status != ZX_OK) {
    return result->status;
  }

  sysmem::BufferCollectionInfo_2& collection_info = result->buffer_collection_info;

  if (!collection_info.settings.has_image_format_constraints ||
      index >= collection_info.buffer_count) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  ZX_DEBUG_ASSERT(collection_info.settings.image_format_constraints.pixel_format.type ==
                  sysmem::PixelFormatType::BGRA32);
  ZX_DEBUG_ASSERT(
      collection_info.settings.image_format_constraints.pixel_format.has_format_modifier);
  ZX_DEBUG_ASSERT(
      collection_info.settings.image_format_constraints.pixel_format.format_modifier.value ==
      sysmem::FORMAT_MODIFIER_LINEAR);

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
  status = amlogic_canvas_config(&canvas_, collection_info.buffers[index].vmo.release(),
                                 collection_info.buffers[index].vmo_usable_start, &canvas_info,
                                 &local_canvas_idx);
  if (status != ZX_OK) {
    DISP_ERROR("Could not configure canvas: %d\n", status);
    status = ZX_ERR_NO_RESOURCES;
    return status;
  }
  fbl::AutoLock lock(&image_lock_);
  import_info->canvas = canvas_;
  import_info->canvas_idx = local_canvas_idx;
  import_info->image_height = image->height;
  import_info->image_width = image->width;
  import_info->image_stride = minimum_row_bytes;
  image->handle = reinterpret_cast<uint64_t>(import_info.get());
  imported_images_.push_back(std::move(import_info));
  return status;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
void AstroDisplay::DisplayControllerImplReleaseImage(image_t* image) {
  fbl::AutoLock lock(&image_lock_);
  auto info = reinterpret_cast<ImageInfo*>(image->handle);
  imported_images_.erase(*info);
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
uint32_t AstroDisplay::DisplayControllerImplCheckConfiguration(
    const display_config_t** display_configs, size_t display_count, uint32_t** layer_cfg_results,
    size_t* layer_cfg_result_count) {
  if (display_count != 1) {
    ZX_DEBUG_ASSERT(display_count == 0);
    return CONFIG_DISPLAY_OK;
  }
  ZX_DEBUG_ASSERT(display_configs[0]->display_id == PANEL_DISPLAY_ID);

  fbl::AutoLock lock(&display_lock_);

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

  if (success) {
    // Make sure ther layer configuration is supported
    const primary_layer_t& layer = display_configs[0]->layer_list[0]->cfg.primary;
    frame_t frame = {
        .x_pos = 0,
        .y_pos = 0,
        .width = width_,
        .height = height_,
    };
    success =
        display_configs[0]->layer_list[0]->type == LAYER_TYPE_PRIMARY &&
        layer.transform_mode == FRAME_TRANSFORM_IDENTITY && layer.image.width == width_ &&
        layer.image.height == height_ && memcmp(&layer.dest_frame, &frame, sizeof(frame_t)) == 0 &&
        memcmp(&layer.src_frame, &frame, sizeof(frame_t)) == 0 && layer.alpha_mode == ALPHA_DISABLE;
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
void AstroDisplay::DisplayControllerImplApplyConfiguration(const display_config_t** display_configs,
                                                           size_t display_count) {
  ZX_DEBUG_ASSERT(display_configs);

  fbl::AutoLock lock(&display_lock_);

  if (display_count == 1 && display_configs[0]->layer_count) {
    if (!full_init_done_) {
      zx_status_t status;
      if ((status = DisplayInit()) != ZX_OK) {
        DISP_ERROR("Display Hardware Initialization failed! %d\n", status);
        ZX_ASSERT(0);
      }
      full_init_done_ = true;
    }

    // Since Astro does not support plug'n play (fixed display), there is no way
    // a checked configuration could be invalid at this point.
    auto info =
        reinterpret_cast<ImageInfo*>(display_configs[0]->layer_list[0]->cfg.primary.image.handle);
    current_image_valid_ = true;
    current_image_ = display_configs[0]->layer_list[0]->cfg.primary.image.handle;
    osd_->FlipOnVsync(info->canvas_idx, display_configs[0]);
  } else {
    current_image_valid_ = false;
    if (full_init_done_) {
      {
        fbl::AutoLock lock2(&capture_lock_);
        if (capture_active_id_ != INVALID_ID) {
          // there's an active capture. stop it before disabling osd
          vpu_->CaptureDone();
          capture_active_id_ = INVALID_ID;
        }
      }
      osd_->Disable();
    }
  }

  // If bootloader does not enable any of the display hardware, no vsync will be generated.
  // This fakes a vsync to let clients know we are ready until we actually initialize hardware
  if (!full_init_done_) {
    if (dc_intf_.is_valid()) {
      if (display_count == 0 || display_configs[0]->layer_count == 0) {
        dc_intf_.OnDisplayVsync(kDisplayId, zx_clock_get_monotonic(), nullptr, 0);
      }
    }
  }
}

void AstroDisplay::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

void AstroDisplay::DdkRelease() {
  if (osd_) {
    osd_->Release();
  }
  vsync_irq_.destroy();
  thrd_join(vsync_thread_, nullptr);
  vd1_wr_irq_.destroy();
  thrd_join(capture_thread_, nullptr);
  delete this;
}

zx_status_t AstroDisplay::DdkGetProtocol(uint32_t proto_id, void* out_protocol) {
  auto* proto = static_cast<ddk::AnyProtocol*>(out_protocol);
  proto->ctx = this;
  switch (proto_id) {
    case ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL:
      proto->ops = &display_controller_impl_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_DISPLAY_CAPTURE_IMPL:
      proto->ops = &display_capture_impl_protocol_ops_;
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

// This function detect the panel type based.
void AstroDisplay::PopulatePanelType() {
  uint8_t pt;
  if ((gpio_config_in(&gpio_, GPIO_NO_PULL) == ZX_OK) && (gpio_read(&gpio_, &pt) == ZX_OK)) {
    panel_type_ = pt;
    if ((board_info_.pid == PDEV_PID_ASTRO) || (board_info_.pid == PDEV_PID_NELSON)) {
      DISP_INFO("Detected panel type = %s (%d)\n", panel_type_ ? "P070ACB_FT" : "TV070WSM_FT",
                panel_type_);
    } else if (board_info_.pid == PDEV_PID_SHERLOCK) {
      DISP_INFO("Detected panel type = %s (%d)\n", panel_type_ ? "G101B158_FT" : "TV101WXM_FT",
                panel_type_);
      panel_type_ = static_cast<uint8_t>(pt + PANEL_TV101WXM_FT);
    } else {
      DISP_ERROR("Panel detection attempted on Unsupported hardware\n");
      ZX_ASSERT(0);
    }
  } else {
    panel_type_ = PANEL_UNKNOWN;
    DISP_ERROR("Failed to detect a valid panel\n");
  }
}

zx_status_t AstroDisplay::SetupDisplayInterface() {
  fbl::AutoLock lock(&display_lock_);

  // Support Astro, Sherlock and Nelson at the moment
  if ((board_info_.pid != PDEV_PID_ASTRO) &&
      (board_info_.pid != PDEV_PID_SHERLOCK) &&
      (board_info_.pid != PDEV_PID_NELSON) ) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  format_ = ZX_PIXEL_FORMAT_RGB_x888;
  stride_ = ComputeLinearStride(width_, format_);

  if (dc_intf_.is_valid()) {
    added_display_args_t args;
    PopulateAddedDisplayArgs(&args);
    dc_intf_.OnDisplaysChanged(&args, 1, nullptr, 0, nullptr, 0, nullptr);
  }

  return ZX_OK;
}

zx_status_t AstroDisplay::DisplayControllerImplGetSysmemConnection(zx::channel connection) {
  zx_status_t status = sysmem_connect(&sysmem_, connection.release());
  if (status != ZX_OK) {
    DISP_ERROR("Could not connect to sysmem\n");
    return status;
  }

  return ZX_OK;
}

zx_status_t AstroDisplay::DisplayControllerImplSetBufferCollectionConstraints(
    const image_t* config, zx_unowned_handle_t collection) {
  sysmem::BufferCollectionConstraints constraints = {};
  if (config->type == IMAGE_TYPE_CAPTURE) {
    constraints.usage.cpu = sysmem::cpuUsageReadOften | sysmem::cpuUsageWriteOften;
  } else {
    constraints.usage.display = sysmem::displayUsageLayer;
  }
  constraints.has_buffer_memory_constraints = true;
  sysmem::BufferMemoryConstraints& buffer_constraints = constraints.buffer_memory_constraints;
  buffer_constraints.physically_contiguous_required = true;
  buffer_constraints.secure_required = false;
  buffer_constraints.ram_domain_supported = true;
  buffer_constraints.cpu_domain_supported = false;
  buffer_constraints.inaccessible_domain_supported = true;
  buffer_constraints.heap_permitted_count = 2;
  buffer_constraints.heap_permitted[0] = sysmem::HeapType::SYSTEM_RAM;
  buffer_constraints.heap_permitted[1] = sysmem::HeapType::AMLOGIC_SECURE;
  constraints.image_format_constraints_count = 1;
  sysmem::ImageFormatConstraints& image_constraints = constraints.image_format_constraints[0];

  image_constraints.pixel_format.has_format_modifier = true;
  image_constraints.pixel_format.format_modifier.value = sysmem::FORMAT_MODIFIER_LINEAR;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0].type = sysmem::ColorSpaceType::SRGB;
  if (config->type == IMAGE_TYPE_CAPTURE) {
    image_constraints.pixel_format.type = sysmem::PixelFormatType::BGR24;
    image_constraints.min_coded_width = disp_setting_.h_active;
    image_constraints.max_coded_width = disp_setting_.h_active;
    image_constraints.min_coded_height = disp_setting_.v_active;
    image_constraints.max_coded_height = disp_setting_.v_active;
    image_constraints.min_bytes_per_row = ALIGN(
        disp_setting_.h_active * ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_888), kBufferAlignment);
    image_constraints.max_coded_width_times_coded_height =
        disp_setting_.h_active * disp_setting_.v_active;
  } else {
    image_constraints.pixel_format.type = sysmem::PixelFormatType::BGRA32;
  }
  image_constraints.bytes_per_row_divisor = kBufferAlignment;
  image_constraints.start_offset_divisor = kBufferAlignment;

  auto res = sysmem::BufferCollection::Call::SetConstraints(zx::unowned_channel(collection), true,
                                                            constraints);

  if (!res.ok()) {
    DISP_ERROR("Failed to set constraints: %d", res.status());
    return res.status();
  }

  return ZX_OK;
}

void AstroDisplay::DisplayCaptureImplSetDisplayCaptureInterface(
    const display_capture_interface_protocol_t* intf) {
  fbl::AutoLock lock(&capture_lock_);
  capture_intf_ = ddk::DisplayCaptureInterfaceProtocolClient(intf);
  capture_active_id_ = INVALID_ID;
}

zx_status_t AstroDisplay::DisplayCaptureImplImportImageForCapture(zx_unowned_handle_t collection,
                                                                  uint32_t index,
                                                                  uint64_t* out_capture_handle) {
  auto import_capture = std::make_unique<ImageInfo>();
  if (import_capture == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  fbl::AutoLock lock(&capture_lock_);
  zx_status_t status, status2;
  fuchsia_sysmem_BufferCollectionInfo_2 collection_info;
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(collection, &status2,
                                                                  &collection_info);
  if (status != ZX_OK) {
    return status;
  }
  if (status2 != ZX_OK) {
    return status2;
  }

  fbl::Vector<zx::vmo> vmos;
  for (uint32_t i = 0; i < collection_info.buffer_count; ++i) {
    vmos.push_back(zx::vmo(collection_info.buffers[i].vmo));
  }

  if (!collection_info.settings.has_image_format_constraints || index >= vmos.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Ensure the proper format
  ZX_DEBUG_ASSERT(collection_info.settings.image_format_constraints.pixel_format.type ==
                  fuchsia_sysmem_PixelFormatType_BGR24);

  // Allocate a canvas for the capture image
  canvas_info_t canvas_info = {};
  canvas_info.height = collection_info.settings.image_format_constraints.min_coded_height;
  canvas_info.stride_bytes = collection_info.settings.image_format_constraints.min_bytes_per_row;
  canvas_info.wrap = 0;
  canvas_info.blkmode = 0;
  canvas_info.endianness = kCanvasLittleEndian64Bit;
  canvas_info.flags = CANVAS_FLAGS_READ | CANVAS_FLAGS_WRITE;
  uint8_t canvas_idx;
  status = amlogic_canvas_config(&canvas_, vmos[index].release(),
                                 collection_info.buffers[index].vmo_usable_start, &canvas_info,
                                 &canvas_idx);
  if (status != ZX_OK) {
    DISP_ERROR("Could not configure canvas %d\n", status);
    return status;
  }

  // At this point, we have setup a canvas with the BufferCollection-based VMO. Store the
  // capture information
  import_capture->canvas_idx = canvas_idx;
  import_capture->image_height = collection_info.settings.image_format_constraints.min_coded_height;
  import_capture->image_width = collection_info.settings.image_format_constraints.min_coded_width;
  import_capture->image_stride =
      collection_info.settings.image_format_constraints.min_bytes_per_row;
  *out_capture_handle = reinterpret_cast<uint64_t>(import_capture.get());
  imported_captures_.push_back(std::move(import_capture));
  return ZX_OK;
}

zx_status_t AstroDisplay::DisplayCaptureImplStartCapture(uint64_t capture_handle) {
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

zx_status_t AstroDisplay::DisplayCaptureImplReleaseCapture(uint64_t capture_handle) {
  fbl::AutoLock lock(&capture_lock_);
  if (capture_handle == capture_active_id_) {
    return ZX_ERR_SHOULD_WAIT;
  }

  // Find and erase previously imported capture
  auto info = reinterpret_cast<ImageInfo*>(capture_handle);
  if (imported_captures_.erase_if([info](auto& i) { return i.canvas_idx == info->canvas_idx; }) ==
      nullptr) {
    return ZX_ERR_NOT_FOUND;
  }

  return ZX_OK;
}

bool AstroDisplay::DisplayCaptureImplIsCaptureCompleted() {
  fbl::AutoLock lock(&capture_lock_);
  return (capture_active_id_ == INVALID_ID);
}

int AstroDisplay::CaptureThread() {
  zx_status_t status;
  while (true) {
    zx::time timestamp;
    status = vd1_wr_irq_.wait(&timestamp);
    if (status != ZX_OK) {
      DISP_ERROR("Vd1 Wr interrupt wait failed %d\n", status);
      break;
    }
    fbl::AutoLock lock(&capture_lock_);
    vpu_->CaptureDone();
    if (capture_intf_.is_valid()) {
      capture_intf_.OnCaptureComplete();
    }
    capture_active_id_ = INVALID_ID;
  }
  return status;
}

int AstroDisplay::VSyncThread() {
  zx_status_t status;
  while (true) {
    zx::time timestamp;
    status = vsync_irq_.wait(&timestamp);
    if (status != ZX_OK) {
      DISP_ERROR("VSync Interrupt Wait failed\n");
      break;
    }
    fbl::AutoLock lock(&display_lock_);
    uint64_t live[] = {current_image_};
    bool current_image_valid = current_image_valid_;
    if (dc_intf_.is_valid()) {
      dc_intf_.OnDisplayVsync(kDisplayId, timestamp.get(), live, current_image_valid);
    }
  }

  return status;
}

// TODO(payamm): make sure unbind/release are called if we return error
zx_status_t AstroDisplay::Bind() {
  composite_protocol_t composite;

  auto status = device_get_protocol(parent_, ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get composite protocol\n");
    return status;
  }

  size_t actual;
  composite_get_components(&composite, components_, fbl::count_of(components_), &actual);
  if (actual != fbl::count_of(components_)) {
    DISP_ERROR("could not get components\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  status = device_get_protocol(components_[COMPONENT_PDEV], ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get PDEV protocol\n");
    return status;
  }

  dsi_impl_protocol_t dsi;
  status = device_get_protocol(components_[COMPONENT_DSI], ZX_PROTOCOL_DSI_IMPL, &dsi);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get DSI_IMPL protocol\n");
    return status;
  }
  dsiimpl_ = &dsi;

  // Get board info
  status = pdev_get_board_info(&pdev_, &board_info_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not obtain board info\n");
    return status;
  }

  if ((board_info_.pid == PDEV_PID_ASTRO) || (board_info_.pid == PDEV_PID_NELSON)) {
    width_ = ASTRO_DISPLAY_WIDTH;
    height_ = ASTRO_DISPLAY_HEIGHT;
  } else if (board_info_.pid == PDEV_PID_SHERLOCK) {
    width_ = SHERLOCK_DISPLAY_WIDTH;
    height_ = SHERLOCK_DISPLAY_HEIGHT;
  } else {
    DISP_ERROR("Running on Unsupported hardware. Use at your own risk\n");
  }

  // Obtain GPIO Protocol for Panel reset
  status = device_get_protocol(components_[COMPONENT_PANEL_GPIO], ZX_PROTOCOL_GPIO, &gpio_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not obtain GPIO protocol.\n");
    return status;
  }

  status = device_get_protocol(components_[COMPONENT_SYSMEM], ZX_PROTOCOL_SYSMEM, &sysmem_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get Display SYSMEM protocol\n");
    return status;
  }

  status = device_get_protocol(components_[COMPONENT_CANVAS], ZX_PROTOCOL_AMLOGIC_CANVAS, &canvas_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not obtain CANVAS protocol\n");
    return status;
  }

  status = pdev_get_bti(&pdev_, 0, bti_.reset_and_get_address());
  if (status != ZX_OK) {
    DISP_ERROR("Could not get BTI handle\n");
    return status;
  }

  // Setup Display Interface
  status = SetupDisplayInterface();
  if (status != ZX_OK) {
    DISP_ERROR("Astro display setup failed! %d\n", status);
    return status;
  }

  // Map VSync Interrupt
  status = pdev_get_interrupt(&pdev_, IRQ_VSYNC, 0, vsync_irq_.reset_and_get_address());
  if (status != ZX_OK) {
    DISP_ERROR("Could not map vsync interrupt\n");
    return status;
  }

  // Map VD1_WR Interrupt (used for capture)
  status = pdev_get_interrupt(&pdev_, IRQ_VD1_WR, 0, vd1_wr_irq_.reset_and_get_address());
  if (status != ZX_OK) {
    DISP_ERROR("Could not map vd1 wr interrupt\n");
    return status;
  }

  auto start_thread = [](void* arg) { return static_cast<AstroDisplay*>(arg)->VSyncThread(); };
  status = thrd_create_with_name(&vsync_thread_, start_thread, this, "vsync_thread");
  if (status != ZX_OK) {
    DISP_ERROR("Could not create vsync_thread\n");
    return status;
  }

  auto vd_thread = [](void* arg) { return static_cast<AstroDisplay*>(arg)->CaptureThread(); };
  status = thrd_create_with_name(&capture_thread_, vd_thread, this, "capture_thread");
  if (status != ZX_OK) {
    DISP_ERROR("Could not create capture_thread\n");
    return status;
  }

  // Set profile for vsync thread.
  // TODO(40858): Migrate to the role-based API when available, instead of hard
  // coding parameters.
  {
    const zx_duration_t capacity = ZX_USEC(500);
    const zx_duration_t deadline = ZX_MSEC(8);
    const zx_duration_t period = deadline;

    zx_handle_t profile = ZX_HANDLE_INVALID;
    if ((status = device_get_deadline_profile(this->zxdev(), capacity, deadline, period,
                                              "dev/display/astro-display/vsync_thread",
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

  auto cleanup = fbl::MakeAutoCall([&]() { DdkRelease(); });

  status = DdkAdd("astro-display");
  if (status != ZX_OK) {
    DISP_ERROR("Could not add device\n");
    return status;
  }

  cleanup.cancel();
  return ZX_OK;
}

void AstroDisplay::Dump() {
  DISP_INFO("#############################\n");
  DISP_INFO("Dumping disp_setting structure:\n");
  DISP_INFO("#############################\n");
  DISP_INFO("h_active = 0x%x (%u)\n", disp_setting_.h_active, disp_setting_.h_active);
  DISP_INFO("v_active = 0x%x (%u)\n", disp_setting_.v_active, disp_setting_.v_active);
  DISP_INFO("h_period = 0x%x (%u)\n", disp_setting_.h_period, disp_setting_.h_period);
  DISP_INFO("v_period = 0x%x (%u)\n", disp_setting_.v_period, disp_setting_.v_period);
  DISP_INFO("hsync_width = 0x%x (%u)\n", disp_setting_.hsync_width, disp_setting_.hsync_width);
  DISP_INFO("hsync_bp = 0x%x (%u)\n", disp_setting_.hsync_bp, disp_setting_.hsync_bp);
  DISP_INFO("hsync_pol = 0x%x (%u)\n", disp_setting_.hsync_pol, disp_setting_.hsync_pol);
  DISP_INFO("vsync_width = 0x%x (%u)\n", disp_setting_.vsync_width, disp_setting_.vsync_width);
  DISP_INFO("vsync_bp = 0x%x (%u)\n", disp_setting_.vsync_bp, disp_setting_.vsync_bp);
  DISP_INFO("vsync_pol = 0x%x (%u)\n", disp_setting_.vsync_pol, disp_setting_.vsync_pol);
  DISP_INFO("lcd_clock = 0x%x (%u)\n", disp_setting_.lcd_clock, disp_setting_.lcd_clock);
  DISP_INFO("lane_num = 0x%x (%u)\n", disp_setting_.lane_num, disp_setting_.lane_num);
  DISP_INFO("bit_rate_max = 0x%x (%u)\n", disp_setting_.bit_rate_max, disp_setting_.bit_rate_max);
  DISP_INFO("clock_factor = 0x%x (%u)\n", disp_setting_.clock_factor, disp_setting_.clock_factor);
}

// main bind function called from dev manager
zx_status_t astro_display_bind(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<astro_display::AstroDisplay>(&ac, parent);
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

static constexpr zx_driver_ops_t astro_display_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = astro_display_bind;
  return ops;
}();

}  // namespace astro_display

// clang-format off
ZIRCON_DRIVER_BEGIN(astro_display, astro_display::astro_display_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_DISPLAY),
ZIRCON_DRIVER_END(astro_display)
