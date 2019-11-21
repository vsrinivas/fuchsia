// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ddk-interface.h"

#include <fuchsia/sysmem/c/fidl.h>

#include <fbl/alloc_checker.h>

namespace hi_display {

// List of supported pixel formats
zx_pixel_format_t kSupportedPixelFormats[] = {ZX_PIXEL_FORMAT_RGB_x888};

void HiDisplay::PopulateAddedDisplayArgs(added_display_args_t* args) {
  args->display_id = kDisplayId;
  args->edid_present = true;
  args->panel.params.height = height_;
  args->panel.params.width = width_;
  args->panel.params.refresh_rate_e2 = 3000;  // Just guess that it's 30fps
  args->pixel_format_list = kSupportedPixelFormats;
  args->pixel_format_count = countof(kSupportedPixelFormats);
  args->cursor_info_count = 0;
}

void HiDisplay::DisplayControllerImplSetDisplayControllerInterface(
    const display_controller_interface_protocol_t* intf) {
  fbl::AutoLock lock(&display_lock_);
  dc_intf_ = ddk::DisplayControllerInterfaceProtocolClient(intf);
  added_display_args_t args;
  PopulateAddedDisplayArgs(&args);
  dc_intf_.OnDisplaysChanged(&args, 1, nullptr, 0, nullptr, 0, nullptr);
}

zx_status_t HiDisplay::DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo,
                                                           size_t offset) {
  zx_status_t status = ZX_OK;

  if (image->type != IMAGE_TYPE_SIMPLE || image->pixel_format != kSupportedPixelFormats[0]) {
    status = ZX_ERR_INVALID_ARGS;
    return status;
  }

  void* new_handle = malloc(1);
  image->handle = reinterpret_cast<uint64_t>(new_handle);

  return status;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t HiDisplay::DisplayControllerImplImportImage(image_t* image, zx_unowned_handle_t handle,
                                                        uint32_t index) {
  zx_status_t status = ZX_OK;

  if (image->type != IMAGE_TYPE_SIMPLE || image->pixel_format != kSupportedPixelFormats[0]) {
    status = ZX_ERR_INVALID_ARGS;
    return status;
  }

  void* new_handle = malloc(1);
  image->handle = reinterpret_cast<uint64_t>(new_handle);

  return status;
}

void HiDisplay::DisplayControllerImplReleaseImage(image_t* image) {
  free(reinterpret_cast<void*>(image->handle));
}

uint32_t HiDisplay::DisplayControllerImplCheckConfiguration(
    const display_config_t** display_configs, size_t display_count, uint32_t** layer_cfg_results,
    size_t* layer_cfg_result_count) {
  if (display_count != 1) {
    ZX_DEBUG_ASSERT(display_count == 0);
    return CONFIG_DISPLAY_OK;
  }
  ZX_DEBUG_ASSERT(display_configs[0]->display_id == PANEL_DISPLAY_ID);

  fbl::AutoLock lock(&display_lock_);

  bool success = true;
  if (display_configs[0]->layer_count > kMaxLayer) {
    success = false;
  } else {
    const primary_layer_t& layer = display_configs[0]->layer_list[0]->cfg.primary;
    frame_t frame = {
        .x_pos = 0,
        .y_pos = 0,
        .width = width_,
        .height = height_,
    };
    success = display_configs[0]->layer_list[0]->type == LAYER_TYPE_PRIMARY &&
              layer.transform_mode == FRAME_TRANSFORM_IDENTITY && layer.image.width == width_ &&
              layer.image.height == height_ &&
              memcmp(&layer.dest_frame, &frame, sizeof(frame_t)) == 0 &&
              memcmp(&layer.src_frame, &frame, sizeof(frame_t)) == 0 &&
              display_configs[0]->cc_flags == 0 && layer.alpha_mode == ALPHA_DISABLE;
  }
  if (!success) {
    layer_cfg_results[0][0] = CLIENT_MERGE_BASE;
    for (unsigned i = 1; i < display_configs[0]->layer_count; i++) {
      layer_cfg_results[0][i] = CLIENT_MERGE_SRC;
    }
  }
  return CONFIG_DISPLAY_OK;
}

void HiDisplay::DisplayControllerImplApplyConfiguration(const display_config_t** display_configs,
                                                        size_t display_count) {
  ZX_DEBUG_ASSERT(display_configs);

  fbl::AutoLock lock(&display_lock_);

  uint64_t addr;
  if (display_count == 1 && display_configs[0]->layer_count) {
    // Only support one display.
    addr = reinterpret_cast<uint64_t>(display_configs[0]->layer_list[0]->cfg.primary.image.handle);
    current_image_valid_ = true;
    current_image_ = addr;
  } else {
    current_image_valid_ = false;
  }
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t HiDisplay::DisplayControllerImplGetSysmemConnection(zx::channel connection) {
  zx_status_t status = sysmem_connect(&sysmem_, connection.release());
  if (status != ZX_OK) {
    DISP_ERROR("Could not connect to sysmem\n");
    return status;
  }

  return ZX_OK;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t HiDisplay::DisplayControllerImplSetBufferCollectionConstraints(
    const image_t* config, zx_unowned_handle_t collection) {
  fuchsia_sysmem_BufferCollectionConstraints constraints = {};
  constraints.usage.display = fuchsia_sysmem_displayUsageLayer;
  constraints.has_buffer_memory_constraints = true;
  fuchsia_sysmem_BufferMemoryConstraints& buffer_constraints =
      constraints.buffer_memory_constraints;
  buffer_constraints.min_size_bytes = 0;
  buffer_constraints.max_size_bytes = 0xffffffff;
  buffer_constraints.physically_contiguous_required = true;
  buffer_constraints.secure_required = false;
  buffer_constraints.ram_domain_supported = true;
  buffer_constraints.cpu_domain_supported = false;
  buffer_constraints.heap_permitted_count = 1;
  buffer_constraints.heap_permitted[0] = fuchsia_sysmem_HeapType_SYSTEM_RAM;
  constraints.image_format_constraints_count = 1;
  fuchsia_sysmem_ImageFormatConstraints& image_constraints =
      constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_BGRA32;
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
  image_constraints.bytes_per_row_divisor = 1;
  image_constraints.start_offset_divisor = 1;
  image_constraints.display_width_divisor = 1;
  image_constraints.display_height_divisor = 1;

  zx_status_t status =
      fuchsia_sysmem_BufferCollectionSetConstraints(collection, true, &constraints);

  if (status != ZX_OK) {
    DISP_ERROR("Failed to set constraints");
    return status;
  }

  return ZX_OK;
}

void HiDisplay::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

void HiDisplay::DdkRelease() {
  vsync_shutdown_flag_.store(true);
  thrd_join(vsync_thread_, NULL);
  delete this;
}

zx_status_t HiDisplay::SetupDisplayInterface() {
  zx_status_t status;
  fbl::AllocChecker ac;
  fbl::AutoLock lock(&display_lock_);

  adv7533_ = fbl::make_unique_checked<hi_display::Adv7533>(&ac);
  if (!ac.check()) {
    DISP_ERROR("Failed to create ADV7533 instance\n");
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = adv7533_->Adv7533Init(&pdev_)) != ZX_OK) {
    zxlogf(ERROR, "%s: Error in ADV7533 Initialization %d\n", __FUNCTION__, status);
    return status;
  }

  dsi_ = fbl::make_unique_checked<hi_display::HiDsi>(&ac);
  if (!ac.check()) {
    DISP_ERROR("Failed to create MIPI DSI instance\n");
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = dsi_->DsiInit(parent_)) != ZX_OK) {
    zxlogf(ERROR, "%s: Error in MIPI DSI Initialization %d\n", __FUNCTION__, status);
    return status;
  }

  if ((status = dsi_->GetDisplayResolution(width_, height_)) != ZX_OK) {
    zxlogf(ERROR, "%s: Panel not connected : %d\n", __FUNCTION__, status);
    return status;
  }

  current_image_valid_ = false;

  if (dc_intf_.is_valid()) {
    added_display_args_t args;
    PopulateAddedDisplayArgs(&args);
    dc_intf_.OnDisplaysChanged(&args, 1, nullptr, 0, nullptr, 0, nullptr);
  }

  return status;
}

int HiDisplay::VSyncThread() {
  while (1) {
    zx::nanosleep(zx::deadline_after(zx::sec(1) / kRefreshRateFps));
    if (vsync_shutdown_flag_.load()) {
      break;
    }
    fbl::AutoLock lock(&display_lock_);
    uint64_t live[] = {current_image_};
    bool current_image_valid = current_image_valid_;
    if (dc_intf_.is_valid()) {
      dc_intf_.OnDisplayVsync(kDisplayId, zx_clock_get_monotonic(), live, current_image_valid);
    }
  }

  return 0;
}

zx_status_t HiDisplay::Bind() {
  zx_status_t status = device_get_protocol(parent_, ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    DISP_ERROR("Failed to obtain the display protocol\n");
    return status;
  }

  status = device_get_protocol(parent_, ZX_PROTOCOL_SYSMEM, &sysmem_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get Display SYSMEM protocol\n");
    return status;
  }

  // Setup Display Interface
  status = SetupDisplayInterface();
  if (status != ZX_OK) {
    DISP_ERROR("Hi-display setup failed! %d\n", status);
    return status;
  }

  auto start_thread = [](void* arg) { return static_cast<HiDisplay*>(arg)->VSyncThread(); };
  status = thrd_create_with_name(&vsync_thread_, start_thread, this, "vsync_thread");
  if (status != ZX_OK) {
    DISP_ERROR("Could not create vsync_thread\n");
    return status;
  }

  status = DdkAdd("hi-display");
  if (status != ZX_OK) {
    DISP_ERROR("Could not add device\n");
    return status;
  }

  return status;
}

// main bind function called from dev manager
zx_status_t hikey_display_bind(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<hi_display::HiDisplay>(&ac, parent);
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

static constexpr zx_driver_ops_t hikey_display_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = hikey_display_bind;
  return ops;
}();

}  // namespace hi_display

// clang-format off
ZIRCON_DRIVER_BEGIN(hikey_display, hi_display::hikey_display_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_96BOARDS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_HIKEY960),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_HI_DISPLAY),
ZIRCON_DRIVER_END(hikey_display)
