// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "simple-display.h"

#include <assert.h>
#include <lib/device-protocol/pci.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/pixelformat.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <memory>
#include <utility>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <ddktl/protocol/display/controller.h>
#include <fbl/alloc_checker.h>
#include <hw/pci.h>

// implement display controller protocol
static constexpr uint64_t kDisplayId = 1;

static constexpr uint64_t kImageHandle = 0xdecafc0ffee;

void SimpleDisplay::DisplayControllerImplSetDisplayControllerInterface(
    const display_controller_interface_protocol_t* intf) {
  intf_ = ddk::DisplayControllerInterfaceProtocolClient(intf);

  added_display_args_t args = {};
  args.display_id = kDisplayId;
  args.edid_present = false;
  args.panel.params.height = height_;
  args.panel.params.width = width_;
  args.panel.params.refresh_rate_e2 = 3000;  // Just guess that it's 30fps
  args.pixel_format_list = &format_;
  args.pixel_format_count = 1;

  intf_.OnDisplaysChanged(&args, 1, nullptr, 0, nullptr, 0, nullptr);
}

zx_status_t SimpleDisplay::DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo,
                                                               size_t offset) {
  zx_info_handle_basic_t import_info;
  size_t actual, avail;
  zx_status_t status =
      vmo.get_info(ZX_INFO_HANDLE_BASIC, &import_info, sizeof(import_info), &actual, &avail);
  if (status != ZX_OK) {
    return status;
  }
  if (import_info.koid != framebuffer_koid_) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (image->width != width_ || image->height != height_ || image->pixel_format != format_ ||
      offset != 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  image->handle = kImageHandle;
  return ZX_OK;
}

void SimpleDisplay::DisplayControllerImplReleaseImage(image_t* image) {
  // noop
}

uint32_t SimpleDisplay::DisplayControllerImplCheckConfiguration(
    const display_config_t** display_configs, size_t display_count, uint32_t** layer_cfg_results,
    size_t* layer_cfg_result_count) {
  if (display_count != 1) {
    ZX_DEBUG_ASSERT(display_count == 0);
    return CONFIG_DISPLAY_OK;
  }
  ZX_DEBUG_ASSERT(display_configs[0]->display_id == kDisplayId);
  bool success;
  if (display_configs[0]->layer_count != 1) {
    success = false;
  } else {
    primary_layer_t* layer = &display_configs[0]->layer_list[0]->cfg.primary;
    frame_t frame = {
        .x_pos = 0,
        .y_pos = 0,
        .width = width_,
        .height = height_,
    };
    success = display_configs[0]->layer_list[0]->type == LAYER_TYPE_PRIMARY &&
              layer->transform_mode == FRAME_TRANSFORM_IDENTITY && layer->image.width == width_ &&
              layer->image.height == height_ &&
              memcmp(&layer->dest_frame, &frame, sizeof(frame_t)) == 0 &&
              memcmp(&layer->src_frame, &frame, sizeof(frame_t)) == 0 &&
              display_configs[0]->cc_flags == 0 && layer->alpha_mode == ALPHA_DISABLE;
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

void SimpleDisplay::DisplayControllerImplApplyConfiguration(const display_config_t** display_config,
                                                            size_t display_count) {
  bool has_image = display_count != 0 && display_config[0]->layer_count != 0;
  uint64_t handles[] = {kImageHandle};
  if (intf_.is_valid()) {
    intf_.OnDisplayVsync(kDisplayId, zx_clock_get_monotonic(), handles, has_image);
  }
}

uint32_t SimpleDisplay::DisplayControllerImplComputeLinearStride(uint32_t width,
                                                                 zx_pixel_format_t format) {
  return (width == width_ && format == format_) ? stride_ : 0;
}

zx_status_t SimpleDisplay::DisplayControllerImplAllocateVmo(uint64_t size, zx::vmo* vmo_out) {
  zx_info_handle_count handle_count;
  size_t actual, avail;
  zx_status_t status = framebuffer_mmio_.get_vmo()->get_info(ZX_INFO_HANDLE_COUNT, &handle_count,
                                                             sizeof(handle_count), &actual, &avail);
  if (status != ZX_OK) {
    return status;
  }
  if (handle_count.handle_count != 1) {
    return ZX_ERR_NO_RESOURCES;
  }
  if (size > height_ * stride_ * ZX_PIXEL_FORMAT_BYTES(format_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return framebuffer_mmio_.get_vmo()->duplicate(ZX_RIGHT_SAME_RIGHTS, vmo_out);
}

zx_status_t SimpleDisplay::DisplayControllerImplGetSingleBufferFramebuffer(zx::vmo* vmo_out,
                                                                           uint32_t* out_stride) {
  *out_stride = stride_;
  zx_info_handle_count handle_count;
  size_t actual, avail;
  zx_status_t status = framebuffer_mmio_.get_vmo()->get_info(ZX_INFO_HANDLE_COUNT, &handle_count,
                                                             sizeof(handle_count), &actual, &avail);
  if (status != ZX_OK) {
    return status;
  }
  if (handle_count.handle_count != 1) {
    return ZX_ERR_NO_RESOURCES;
  }
  return framebuffer_mmio_.get_vmo()->duplicate(ZX_RIGHT_SAME_RIGHTS, vmo_out);
}

// implement device protocol

void SimpleDisplay::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void SimpleDisplay::DdkRelease() { delete this; }

// implement driver object:

zx_status_t SimpleDisplay::Bind(const char* name, std::unique_ptr<SimpleDisplay>* vbe_ptr) {
  zx_info_handle_basic_t framebuffer_info;
  size_t actual, avail;
  zx_status_t status = framebuffer_mmio_.get_vmo()->get_info(
      ZX_INFO_HANDLE_BASIC, &framebuffer_info, sizeof(framebuffer_info), &actual, &avail);
  if (status != ZX_OK) {
    printf("%s: failed to id framebuffer: %d\n", name, status);
    return status;
  }
  framebuffer_koid_ = framebuffer_info.koid;

  status = DdkAdd(name);
  if (status != ZX_OK) {
    return status;
  }
  // DevMgr now owns this pointer, release it to avoid destroying the object
  // when device goes out of scope.
  __UNUSED auto ptr = vbe_ptr->release();

  zxlogf(INFO, "%s: initialized display, %u x %u (stride=%u format=%08x)", name, width_, height_,
         stride_, format_);

  return ZX_OK;
}

SimpleDisplay::SimpleDisplay(zx_device_t* parent, ddk::MmioBuffer framebuffer_mmio, uint32_t width,
                             uint32_t height, uint32_t stride, zx_pixel_format_t format)
    : DeviceType(parent),
      framebuffer_mmio_(std::move(framebuffer_mmio)),
      width_(width),
      height_(height),
      stride_(stride),
      format_(format) {}

zx_status_t bind_simple_pci_display_bootloader(zx_device_t* dev, const char* name, uint32_t bar) {
  uint32_t format, width, height, stride;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx_status_t status =
      zx_framebuffer_get_info(get_root_resource(), &format, &width, &height, &stride);
  if (status != ZX_OK) {
    printf("%s: failed to get bootloader dimensions: %d\n", name, status);
    return ZX_ERR_NOT_SUPPORTED;
  }

  return bind_simple_pci_display(dev, name, bar, width, height, stride, format);
}

zx_status_t bind_simple_pci_display(zx_device_t* dev, const char* name, uint32_t bar,
                                    uint32_t width, uint32_t height, uint32_t stride,
                                    zx_pixel_format_t format) {
  pci_protocol_t pci;
  zx_status_t status;
  if (device_get_protocol(dev, ZX_PROTOCOL_PCI, &pci)) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  mmio_buffer_t mmio;
  // map framebuffer window
  status = pci_map_bar_buffer(&pci, bar, ZX_CACHE_POLICY_WRITE_COMBINING, &mmio);
  if (status != ZX_OK) {
    printf("%s: failed to map pci bar %d: %d\n", name, bar, status);
    return status;
  }
  ddk::MmioBuffer framebuffer_mmio(mmio);

  fbl::AllocChecker ac;
  std::unique_ptr<SimpleDisplay> display(
      new (&ac) SimpleDisplay(dev, std::move(framebuffer_mmio), width, height, stride, format));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  return display->Bind(name, &display);
}
