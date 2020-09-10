// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-display.h"

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/image-format/image_format.h>
#include <lib/zx/pmt.h>
#include <lib/zx/time.h>
#include <sys/types.h>
#include <threads.h>
#include <zircon/errors.h>
#include <zircon/limits.h>

#include <algorithm>
#include <iterator>
#include <memory>

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/display/controller.h>
#include <ddktl/device.h>
#include <ddktl/protocol/display/capture.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "fbl/vector.h"
#include "zircon/types.h"

namespace fake_display {
#define DISP_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

namespace {
// List of supported pixel formats
zx_pixel_format_t kSupportedPixelFormats[] = {ZX_PIXEL_FORMAT_RGB_x888};
// Arbitrary dimensions - the same as astro.
constexpr uint32_t kWidth = 1024;
constexpr uint32_t kHeight = 600;

constexpr uint64_t kDisplayId = 1;

constexpr uint32_t kRefreshRateFps = 60;
// Arbitrary slowdown for testing purposes
// TODO(payamm): Randomizing the delay value is more value
constexpr uint64_t kNumOfVsyncsForCapture = 5;  // 5 * 16ms = 80ms
}  // namespace

zx_status_t FakeDisplay::DisplayClampRgbImplSetMinimumRgb(uint8_t minimum_rgb) {
  clamp_rgb_value_ = minimum_rgb;
  return ZX_OK;
}

void FakeDisplay::PopulateAddedDisplayArgs(added_display_args_t* args) {
  args->display_id = kDisplayId;
  args->edid_present = false;
  args->panel.params.height = kHeight;
  args->panel.params.width = kWidth;
  args->panel.params.refresh_rate_e2 = kRefreshRateFps * 100;
  args->pixel_format_list = kSupportedPixelFormats;
  args->pixel_format_count = countof(kSupportedPixelFormats);
  args->cursor_info_count = 0;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
void FakeDisplay::DisplayControllerImplSetDisplayControllerInterface(
    const display_controller_interface_protocol_t* intf) {
  fbl::AutoLock lock(&display_lock_);
  dc_intf_ = ddk::DisplayControllerInterfaceProtocolClient(intf);
  added_display_args_t args;
  PopulateAddedDisplayArgs(&args);
  dc_intf_.OnDisplaysChanged(&args, 1, nullptr, 0, nullptr, 0, nullptr);
}

zx_status_t FakeDisplay::ImportVmoImage(image_t* image, zx::vmo vmo, size_t offset) {
  zx_status_t status = ZX_OK;

  auto import_info = std::make_unique<ImageInfo>();
  if (import_info == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  fbl::AutoLock lock(&image_lock_);

  import_info->vmo = std::move(vmo);
  image->handle = reinterpret_cast<uint64_t>(import_info.get());
  imported_images_.push_back(std::move(import_info));

  return status;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t FakeDisplay::DisplayControllerImplImportImage(image_t* image,
                                                          zx_unowned_handle_t handle,
                                                          uint32_t index) {
  zx_status_t status = ZX_OK;
  auto import_info = std::make_unique<ImageInfo>();
  if (import_info == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  fbl::AutoLock lock(&image_lock_);
  if (image->type != IMAGE_TYPE_SIMPLE || image->pixel_format != kSupportedPixelFormats[0]) {
    status = ZX_ERR_INVALID_ARGS;
    return status;
  }
  zx_status_t status2;
  fuchsia_sysmem_BufferCollectionInfo_2 collection_info;
  status =
      fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(handle, &status2, &collection_info);

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

  import_info->vmo = std::move(vmos[index]);
  image->handle = reinterpret_cast<uint64_t>(import_info.get());
  imported_images_.push_back(std::move(import_info));
  return status;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
void FakeDisplay::DisplayControllerImplReleaseImage(image_t* image) {
  fbl::AutoLock lock(&image_lock_);
  auto info = reinterpret_cast<ImageInfo*>(image->handle);
  imported_images_.erase(*info);
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
uint32_t FakeDisplay::DisplayControllerImplCheckConfiguration(
    const display_config_t** display_configs, size_t display_count, uint32_t** layer_cfg_results,
    size_t* layer_cfg_result_count) {
  if (display_count != 1) {
    ZX_DEBUG_ASSERT(display_count == 0);
    return CONFIG_DISPLAY_OK;
  }
  ZX_DEBUG_ASSERT(display_configs[0]->display_id == kDisplayId);

  fbl::AutoLock lock(&display_lock_);

  bool success;
  if (display_configs[0]->layer_count != 1) {
    success = display_configs[0]->layer_count == 0;
  } else {
    const primary_layer_t& layer = display_configs[0]->layer_list[0]->cfg.primary;
    frame_t frame = {
        .x_pos = 0,
        .y_pos = 0,
        .width = kWidth,
        .height = kHeight,
    };
    success = display_configs[0]->layer_list[0]->type == LAYER_TYPE_PRIMARY &&
              layer.transform_mode == FRAME_TRANSFORM_IDENTITY && layer.image.width == kWidth &&
              layer.image.height == kHeight &&
              memcmp(&layer.dest_frame, &frame, sizeof(frame_t)) == 0 &&
              memcmp(&layer.src_frame, &frame, sizeof(frame_t)) == 0 &&
              display_configs[0]->cc_flags == 0 && layer.alpha_mode == ALPHA_DISABLE;
  }
  if (!success) {
    layer_cfg_results[0][0] = CLIENT_MERGE_BASE;
    for (unsigned i = 1; i < display_configs[0]->layer_count; i++) {
      layer_cfg_results[0][i] = CLIENT_MERGE_SRC;
    }
    layer_cfg_result_count[0] = display_configs[0]->layer_count;
  }
  return CONFIG_DISPLAY_OK;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
void FakeDisplay::DisplayControllerImplApplyConfiguration(const display_config_t** display_configs,
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
zx_status_t FakeDisplay::DisplayControllerImplGetSysmemConnection(zx::channel connection) {
  zx_status_t status = sysmem_connect(&sysmem_, connection.release());
  if (status != ZX_OK) {
    DISP_ERROR("Could not connect to sysmem\n");
    return status;
  }

  return ZX_OK;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t FakeDisplay::DisplayControllerImplSetBufferCollectionConstraints(
    const image_t* config, zx_unowned_handle_t collection) {
  fuchsia_sysmem_BufferCollectionConstraints constraints = {};
  if (config->type == IMAGE_TYPE_CAPTURE) {
    constraints.usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
  } else {
    constraints.usage.display = fuchsia_sysmem_displayUsageLayer;
  }

  constraints.has_buffer_memory_constraints = true;
  fuchsia_sysmem_BufferMemoryConstraints& buffer_constraints =
      constraints.buffer_memory_constraints;
  buffer_constraints.min_size_bytes = 0;
  buffer_constraints.max_size_bytes = 0xffffffff;
  buffer_constraints.physically_contiguous_required = false;
  buffer_constraints.secure_required = false;
  buffer_constraints.ram_domain_supported = true;
  buffer_constraints.cpu_domain_supported = true;
  constraints.image_format_constraints_count = 1;
  fuchsia_sysmem_ImageFormatConstraints& image_constraints =
      constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_BGRA32;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0].type = fuchsia_sysmem_ColorSpaceType_SRGB;
  if (config->type == IMAGE_TYPE_CAPTURE) {
    image_constraints.min_coded_width = kWidth;
    image_constraints.max_coded_width = kWidth;
    image_constraints.min_coded_height = kHeight;
    image_constraints.max_coded_height = kHeight;
    image_constraints.min_bytes_per_row = kWidth * 4;
    image_constraints.max_bytes_per_row = kWidth * 4;
    image_constraints.max_coded_width_times_coded_height = kWidth * kHeight;
  } else {
    image_constraints.min_coded_width = 0;
    image_constraints.max_coded_width = 0xffffffff;
    image_constraints.min_coded_height = 0;
    image_constraints.max_coded_height = 0xffffffff;
    image_constraints.min_bytes_per_row = 0;
    image_constraints.max_bytes_per_row = 0xffffffff;
    image_constraints.max_coded_width_times_coded_height = 0xffffffff;
  }

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

void FakeDisplay::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void FakeDisplay::DisplayCaptureImplSetDisplayCaptureInterface(
    const display_capture_interface_protocol_t* intf) {
  fbl::AutoLock lock(&capture_lock_);
  capture_intf_ = ddk::DisplayCaptureInterfaceProtocolClient(intf);
  capture_active_id_ = INVALID_ID;
}

zx_status_t FakeDisplay::DisplayCaptureImplImportImageForCapture(zx_unowned_handle_t collection,
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

  import_capture->vmo = std::move(vmos[index]);
  *out_capture_handle = reinterpret_cast<uint64_t>(import_capture.get());
  imported_captures_.push_back(std::move(import_capture));
  return ZX_OK;
}

zx_status_t FakeDisplay::DisplayCaptureImplStartCapture(uint64_t capture_handle) {
  fbl::AutoLock lock(&capture_lock_);
  if (capture_active_id_ != INVALID_ID) {
    return ZX_ERR_SHOULD_WAIT;
  }

  // Confirm the handle was previously imported (hence valid)
  auto info = reinterpret_cast<ImageInfo*>(capture_handle);
  if (imported_captures_.find_if([info](auto& i) { return i.vmo.get() == info->vmo.get(); }) ==
      imported_captures_.end()) {
    // invalid handle
    return ZX_ERR_INVALID_ARGS;
  }
  capture_active_id_ = capture_handle;

  return ZX_OK;
}

zx_status_t FakeDisplay::DisplayCaptureImplReleaseCapture(uint64_t capture_handle) {
  fbl::AutoLock lock(&capture_lock_);
  if (capture_handle == capture_active_id_) {
    return ZX_ERR_SHOULD_WAIT;
  }

  // Confirm the handle was previously imported (hence valid)
  auto info = reinterpret_cast<ImageInfo*>(capture_handle);
  if (imported_captures_.erase_if([info](auto& i) { return i.vmo.get() == info->vmo.get(); }) ==
      nullptr) {
    // invalid handle
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

bool FakeDisplay::DisplayCaptureImplIsCaptureCompleted() {
  fbl::AutoLock lock(&capture_lock_);
  return (capture_active_id_ == INVALID_ID);
}

void FakeDisplay::DdkRelease() {
  vsync_shutdown_flag_.store(true);
  if (vsync_thread_running_) {
    // Ignore return value here in case the vsync_thread_ isn't running.
    thrd_join(vsync_thread_, nullptr);
  }
  capture_shutdown_flag_.store(true);
  thrd_join(capture_thread_, nullptr);
  delete this;
}

zx_status_t FakeDisplay::DdkGetProtocol(uint32_t proto_id, void* out_protocol) {
  auto* proto = static_cast<ddk::AnyProtocol*>(out_protocol);
  proto->ctx = this;
  switch (proto_id) {
    case ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL:
      proto->ops = &display_controller_impl_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_DISPLAY_CAPTURE_IMPL:
      proto->ops = &display_capture_impl_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_DISPLAY_CLAMP_RGB_IMPL:
      proto->ops = &display_clamp_rgb_impl_protocol_ops_;
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t FakeDisplay::SetupDisplayInterface() {
  fbl::AutoLock lock(&display_lock_);

  current_image_valid_ = false;

  if (dc_intf_.is_valid()) {
    added_display_args_t args;
    PopulateAddedDisplayArgs(&args);
    dc_intf_.OnDisplaysChanged(&args, 1, nullptr, 0, nullptr, 0, nullptr);
  }

  return ZX_OK;
}

int FakeDisplay::CaptureThread() {
  while (true) {
    zx::nanosleep(zx::deadline_after(zx::sec(1) / kRefreshRateFps));
    if (capture_shutdown_flag_.load()) {
      break;
    }
    {
      fbl::AutoLock lock(&capture_lock_);
      if (capture_intf_.is_valid() && (capture_active_id_ != INVALID_ID) &&
          ++capture_complete_signal_count_ >= kNumOfVsyncsForCapture) {
        {
          fbl::AutoLock lock(&display_lock_);
          if (current_image_) {
            // We have a valid image being displayed. Let's capture it.
            auto src = reinterpret_cast<ImageInfo*>(current_image_);
            auto dst = reinterpret_cast<ImageInfo*>(capture_active_id_);

            size_t src_vmo_size;
            auto status = src->vmo.get_size(&src_vmo_size);
            if (status != ZX_OK) {
              DISP_ERROR("Could not get vmo size of displayed image\n");
              continue;
            }
            size_t dst_vmo_size;
            status = dst->vmo.get_size(&dst_vmo_size);
            if (status != ZX_OK) {
              DISP_ERROR("Could not get vmo size of captured image\n");
              continue;
            }
            if (dst_vmo_size != src_vmo_size) {
              DISP_ERROR("Size mismatch between src (%zu) and dst (%zu)\n", src_vmo_size,
                         dst_vmo_size);
              continue;
            }
            fzl::VmoMapper mapped_src;
            status = mapped_src.Map(src->vmo, 0, src_vmo_size, ZX_VM_PERM_READ);
            if (status != ZX_OK) {
              DISP_ERROR("Could not map source %d\n", status);
              return status;
            }

            fzl::VmoMapper mapped_dst;
            status = mapped_dst.Map(dst->vmo, 0, dst_vmo_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
            if (status != ZX_OK) {
              DISP_ERROR("Could not map destination %d\n", status);
              return status;
            }
            memcpy(mapped_dst.start(), mapped_src.start(), dst_vmo_size);
          }
        }
        capture_intf_.OnCaptureComplete();
        capture_active_id_ = INVALID_ID;
        capture_complete_signal_count_ = 0;
      }
    }
  }
  return ZX_OK;
}

int FakeDisplay::VSyncThread() {
  while (true) {
    zx::nanosleep(zx::deadline_after(zx::sec(1) / kRefreshRateFps));
    if (vsync_shutdown_flag_.load()) {
      break;
    }
    SendVsync();
  }
  return ZX_OK;
}

void FakeDisplay::SendVsync() {
  fbl::AutoLock lock(&display_lock_);
  if (dc_intf_.is_valid()) {
    uint64_t live[] = {current_image_};
    dc_intf_.OnDisplayVsync(kDisplayId, zx_clock_get_monotonic(),
                            current_image_valid_ ? live : nullptr, current_image_valid_);
  }
}

zx_status_t FakeDisplay::Bind(bool start_vsync) {
  composite_protocol_t composite;
  auto status = device_get_protocol(parent_, ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get composite protocol %d\n", status);
    return status;
  }
  size_t actual;
  composite_get_fragments(&composite, fragments_, std::size(fragments_), &actual);
  if (actual != std::size(fragments_)) {
    DISP_ERROR("could not get fragments\n");
    return ZX_ERR_NOT_SUPPORTED;
  }
  status = device_get_protocol(fragments_[FRAGMENT_PDEV], ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get PDEV protocol\n");
    return status;
  }

  status = device_get_protocol(fragments_[FRAGMENT_SYSMEM], ZX_PROTOCOL_SYSMEM, &sysmem_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get Display SYSMEM protocol\n");
    return status;
  }

  // Setup Display Interface
  status = SetupDisplayInterface();
  if (status != ZX_OK) {
    DISP_ERROR("Fake display setup failed! %d\n", status);
    return status;
  }

  if (start_vsync) {
    auto start_thread = [](void* arg) { return static_cast<FakeDisplay*>(arg)->VSyncThread(); };
    status = thrd_create_with_name(&vsync_thread_, start_thread, this, "vsync_thread");
    if (status != ZX_OK) {
      DISP_ERROR("Could not create vsync_thread\n");
      return status;
    }
  }
  vsync_thread_running_ = start_vsync;

  auto c_thread = [](void* arg) { return static_cast<FakeDisplay*>(arg)->CaptureThread(); };
  status = thrd_create_with_name(&capture_thread_, c_thread, this, "capture_thread");
  if (status != ZX_OK) {
    DISP_ERROR("Could not create capture_thread\n");
    return status;
  }

  status = DdkAdd("fake-display");
  if (status != ZX_OK) {
    DISP_ERROR("Could not add device\n");
    return status;
  }

  return ZX_OK;
}

}  // namespace fake_display
