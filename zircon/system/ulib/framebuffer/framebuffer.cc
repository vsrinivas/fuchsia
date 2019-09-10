// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/framebuffer/framebuffer.h"

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <lib/fidl/coding.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/device/display-controller.h>
#include <zircon/pixelformat.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>

namespace fhd = ::llcpp::fuchsia::hardware::display;

static zx_handle_t device_handle = ZX_HANDLE_INVALID;

std::unique_ptr<fhd::Controller::SyncClient> dc_client;

static uint64_t display_id;
static uint64_t layer_id;

static int32_t width;
static int32_t height;
static int32_t stride;
static zx_pixel_format_t format;
static bool type_set;
static uint32_t image_type;

static zx_handle_t vmo = ZX_HANDLE_INVALID;

static bool inited = false;
static bool in_single_buffer_mode;

static zx_status_t fb_import_image(zx_handle_t handle, uint32_t type, uint64_t* id_out);
static void fb_release_image(uint64_t id);

// Imports an event handle to use for image synchronization. This function
// always consumes |handle|. Id must be unique and not equal to FB_INVALID_ID.
static zx_status_t fb_import_event(zx_handle_t handle, uint64_t id);
static void fb_release_event(uint64_t id);

// Presents the image identified by |image_id|.
//
// If |wait_event_id| corresponds to an imported event, then driver will wait for
// for ZX_EVENT_SIGNALED before using the buffer. If |signal_event_id| corresponds
// to an imported event, then the driver will signal ZX_EVENT_SIGNALED when it is
// done with the image.
static zx_status_t fb_present_image(uint64_t image_id, uint64_t wait_event_id,
                                    uint64_t signal_event_id);

static zx_status_t set_layer_config(uint64_t layer_id, uint32_t width, uint32_t height,
                                    zx_pixel_format_t format, int32_t type) {
  fhd::ImageConfig config = {
      .width = width,
      .height = height,
      .pixel_format = format,
      .type = static_cast<uint32_t>(type),
  };
  return dc_client->SetLayerPrimaryConfig(layer_id, config).status();
}

zx_status_t fb_bind(bool single_buffer, const char** err_msg_out) {
  const char* err_msg;
  if (!err_msg_out) {
    err_msg_out = &err_msg;
  }
  *err_msg_out = "";

  if (inited) {
    *err_msg_out = "framebufer already initialzied";
    return ZX_ERR_ALREADY_BOUND;
  }

  // TODO(stevensd): Don't hardcode display controller 0
  fbl::unique_fd dc_fd(open("/dev/class/display-controller/000", O_RDWR));
  if (!dc_fd) {
    *err_msg_out = "Failed to open display controller";
    return ZX_ERR_NO_RESOURCES;
  }

  zx::channel device_server, device_client;
  zx_status_t status = zx::channel::create(0, &device_server, &device_client);
  if (status != ZX_OK) {
    *err_msg_out = "Failed to create device channel";
    return status;
  }

  zx::channel dc_server, dc_client_channel;
  status = zx::channel::create(0, &dc_server, &dc_client_channel);
  if (status != ZX_OK) {
    *err_msg_out = "Failed to create controller channel";
    return status;
  }

  fzl::FdioCaller caller(std::move(dc_fd));
  auto open_status = fhd::Provider::Call::OpenController(caller.channel(), std::move(device_server),
                                                         std::move(dc_server));
  if (open_status.status() != ZX_OK) {
    *err_msg_out = "Failed to call service handle";
    return open_status.status();
  }
  if (status != ZX_OK) {
    *err_msg_out = "Failed to open controller";
    return status;
  }

  device_handle = device_client.release();
  dc_client = std::make_unique<fhd::Controller::SyncClient>(std::move(dc_client_channel));

  fbl::AutoCall close_dc_handle([]() {
    zx_handle_close(device_handle);
    dc_client.reset();
    device_handle = ZX_HANDLE_INVALID;
  });

  zx_pixel_format_t pixel_format;
  bool has_display = false;
  fhd::Mode mode;

  do {
    zx_status_t status = dc_client->HandleEvents(fhd::Controller::EventHandlers{
        .displays_changed =
            [&has_display, &pixel_format, &mode](fidl::VectorView<fhd::Info> added,
                                                 fidl::VectorView<uint64_t> removed) {
              has_display = true;
              display_id = added[0].id;
              mode = added[0].modes[0];
              pixel_format = added[0].pixel_format[0];
              // We're guaranteed that added contains at least one display, since we haven't
              // been notified of any displays to remove.
              return ZX_OK;
            },
        .vsync = [](uint64_t display_id, uint64_t timestamp,
                    fidl::VectorView<uint64_t> images) { return ZX_ERR_NEXT; },
        .client_ownership_change = [](bool has_ownership) { return ZX_ERR_NEXT; },
        .unknown = []() { return ZX_ERR_STOP; }});

    if (status != ZX_OK && status != ZX_ERR_NEXT) {
      return status;
    }
  } while (!has_display);

  auto stride_rsp = dc_client->ComputeLinearImageStride(mode.horizontal_resolution, pixel_format);
  if (!stride_rsp.ok()) {
    *err_msg_out = stride_rsp.error();
    return stride_rsp.status();
  }

  auto create_layer_rsp = dc_client->CreateLayer();
  if (!create_layer_rsp.ok()) {
    *err_msg_out = "Create layer call failed";
    return create_layer_rsp.status();
  }
  if (create_layer_rsp->res != ZX_OK) {
    *err_msg_out = "Failed to create layer";
    status = create_layer_rsp->res;
    return status;
  }

  auto layers_rsp = dc_client->SetDisplayLayers(
      display_id, fidl::VectorView<uint64_t>(&create_layer_rsp->layer_id, 1));
  if (!layers_rsp.ok()) {
    *err_msg_out = layers_rsp.error();
    return layers_rsp.status();
  }

  if ((status = set_layer_config(create_layer_rsp->layer_id, mode.horizontal_resolution,
                                 mode.vertical_resolution, pixel_format, IMAGE_TYPE_SIMPLE)) !=
      ZX_OK) {
    *err_msg_out = "Failed to set layer config";
    return status;
  }

  layer_id = create_layer_rsp->layer_id;

  width = mode.horizontal_resolution;
  height = mode.vertical_resolution;
  format = pixel_format;
  stride = stride_rsp->stride;

  type_set = false;

  inited = true;

  fbl::AutoCall clear_inited([]() { inited = false; });

  zx::vmo local_vmo;

  if (single_buffer) {
    uint32_t size = stride * height * ZX_PIXEL_FORMAT_BYTES(format);
    auto alloc_rsp = dc_client->AllocateVmo(size);
    if (!alloc_rsp.ok()) {
      status = alloc_rsp.status();
      *err_msg_out = "Failed to alloc vmo in call";
      return status;
    }

    if (alloc_rsp->res != ZX_OK) {
      status = alloc_rsp->res;
      *err_msg_out = "Failed to alloc vmo";
      return status;
    }

    local_vmo = std::move(alloc_rsp->vmo);

    // Failure to set the cache policy isn't a fatal error
    zx_vmo_set_cache_policy(local_vmo.get(), ZX_CACHE_POLICY_WRITE_COMBINING);

    zx_handle_t dup;
    if ((status = zx_handle_duplicate(local_vmo.get(), ZX_RIGHT_SAME_RIGHTS, &dup)) != ZX_OK) {
      *err_msg_out = "Couldn't duplicate vmo\n";
      return status;
    }

    // fb_(present|import)_image expect to not be in single buffer
    // mode, so make sure this is false for now. It will get set properly later.
    in_single_buffer_mode = false;

    uint64_t image_id;
    if ((status = fb_import_image(dup, 0, &image_id)) != ZX_OK) {
      *err_msg_out = "Couldn't import framebuffer";
      return status;
    }

    if ((status = fb_present_image(image_id, INVALID_ID, INVALID_ID)) != ZX_OK) {
      *err_msg_out = "Failed to present single_buffer mode framebuffer";
      return status;
    }
  }

  in_single_buffer_mode = single_buffer;

  clear_inited.cancel();
  vmo = local_vmo.release();
  close_dc_handle.cancel();

  return ZX_OK;
}

void fb_release() {
  if (!inited) {
    return;
  }

  zx_handle_close(device_handle);
  dc_client.reset();
  device_handle = ZX_HANDLE_INVALID;

  if (in_single_buffer_mode) {
    zx_handle_close(vmo);
    vmo = ZX_HANDLE_INVALID;
  }

  inited = false;
}

void fb_get_config(uint32_t* width_out, uint32_t* height_out, uint32_t* linear_stride_px_out,
                   zx_pixel_format_t* format_out) {
  ZX_ASSERT(inited);

  *width_out = width;
  *height_out = height;
  *format_out = format;
  *linear_stride_px_out = stride;
}

zx_handle_t fb_get_single_buffer() {
  ZX_ASSERT(inited && in_single_buffer_mode);
  return vmo;
}

zx_status_t fb_import_image(zx_handle_t handle, uint32_t type, uint64_t* id_out) {
  ZX_ASSERT(inited && !in_single_buffer_mode);
  zx_status_t status;

  if (type_set && type != image_type) {
    return ZX_ERR_BAD_STATE;
  } else if (!type_set && type != IMAGE_TYPE_SIMPLE) {
    if ((status = set_layer_config(layer_id, width, height, format, type)) != ZX_OK) {
      return status;
    }
    image_type = type;
    type_set = true;
  }

  fhd::ImageConfig config = {
      .width = static_cast<uint32_t>(width),
      .height = static_cast<uint32_t>(height),
      .pixel_format = format,
      .type = type,
  };

  auto import_rsp = dc_client->ImportVmoImage(config, zx::vmo(handle), 0);
  if (!import_rsp.ok()) {
    return import_rsp.status();
  }

  if (import_rsp->res != ZX_OK) {
    return import_rsp->res;
  }

  *id_out = import_rsp->image_id;
  return ZX_OK;
}

void fb_release_image(uint64_t image_id) {
  ZX_ASSERT(inited && !in_single_buffer_mode);

  // There's nothing meaningful we can do if this call fails
  dc_client->ReleaseImage(image_id);
}

zx_status_t fb_import_event(zx_handle_t handle, uint64_t id) {
  ZX_ASSERT(inited && !in_single_buffer_mode);
  return dc_client->ImportEvent(zx::event(handle), id).status();
}

void fb_release_event(uint64_t id) {
  ZX_ASSERT(inited && !in_single_buffer_mode);
  // There's nothing meaningful we can do if this call fails
  dc_client->ReleaseEvent(id);
}

zx_status_t fb_present_image(uint64_t image_id, uint64_t wait_event_id, uint64_t signal_event_id) {
  ZX_ASSERT(inited && !in_single_buffer_mode);
  auto rsp = dc_client->SetLayerImage(layer_id, image_id, wait_event_id, signal_event_id);
  if (!rsp.ok()) {
    return rsp.status();
  }

  return dc_client->ApplyConfig().status();
}
