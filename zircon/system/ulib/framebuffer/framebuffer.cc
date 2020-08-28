// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/framebuffer/framebuffer.h"

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/coding.h>
#include <lib/fit/defer.h>
#include <lib/image-format-llcpp/image-format-llcpp.h>
#include <lib/image-format/image_format.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/pixelformat.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <variant>

#include <ddk/protocol/display/controller.h>
#include <fbl/unique_fd.h>

namespace fhd = ::llcpp::fuchsia::hardware::display;
namespace sysmem = ::llcpp::fuchsia::sysmem;

static zx_handle_t device_handle = ZX_HANDLE_INVALID;

std::unique_ptr<fhd::Controller::SyncClient> dc_client;
std::unique_ptr<sysmem::Allocator::SyncClient> sysmem_allocator;

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

static zx_status_t fb_import_image(uint64_t collection_id, uint32_t index, uint32_t type,
                                   uint64_t* id_out);

// Always import to collection id 1.
static const uint32_t kCollectionId = 1;

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

template <typename T>
class EndpointOrError {
 public:
  static EndpointOrError<T> Create() {
    zx::channel token_server, token_client;
    zx_status_t status = zx::channel::create(0, &token_server, &token_client);
    if (status != ZX_OK) {
      return EndpointOrError<T>(status);
    }
    return EndpointOrError<T>(std::move(token_server), std::move(token_client));
  }

  EndpointOrError(zx_status_t status) : internal_(status) {}
  EndpointOrError(zx::channel server, zx::channel client)
      : internal_(std::make_pair(std::move(server), T(std::move(client)))) {}

  bool ok() { return internal_.index() == 1; }
  zx_status_t status() { return std::get<0>(internal_); }

  zx::channel TakeServer() { return std::move(std::get<1>(internal_).first); }
  T& operator*() { return std::get<1>(internal_).second; }
  T* operator->() { return &std::get<1>(internal_).second; }

 private:
  std::variant<zx_status_t, std::pair<zx::channel, T>> internal_;
};

#define CHECK_RSP(rsp, err_msg) \
  if (!(rsp).ok()) {            \
    *err_msg_out = err_msg;     \
    return (rsp).status();      \
  }

#define CHECKED_CALL(func, err_msg) \
  {                                 \
    auto rsp = func;                \
    CHECK_RSP(rsp, err_msg);        \
  }

static zx_status_t create_buffer_collection(
    const char** err_msg_out,
    std::unique_ptr<sysmem::BufferCollection::SyncClient>* collection_client) {
  auto token = EndpointOrError<sysmem::BufferCollectionToken::SyncClient>::Create();
  CHECK_RSP(token, "Failed to create collection channel");
  CHECKED_CALL(sysmem_allocator->AllocateSharedCollection(token.TakeServer()),
               "Failed to allocate shared collection");
  auto display_token = EndpointOrError<sysmem::BufferCollectionToken::SyncClient>::Create();
  CHECK_RSP(display_token, "Failed to allocate display token");
  CHECKED_CALL(token->Duplicate(ZX_RIGHT_SAME_RIGHTS, display_token.TakeServer()),
               "Failed to duplicate token");
  CHECKED_CALL(token->Sync(), "Failed to sync token");

  auto import_rsp = dc_client->ImportBufferCollection(kCollectionId,
                                                      std::move(*display_token->mutable_channel()));
  CHECK_RSP(import_rsp, "Failed to import buffer collection");
  if (import_rsp->res != ZX_OK) {
    *err_msg_out = "Import buffer collection error";
    return import_rsp->res;
  }

  fhd::ImageConfig config = {
      .width = static_cast<uint32_t>(width),
      .height = static_cast<uint32_t>(height),
      .pixel_format = format,
      .type = IMAGE_TYPE_SIMPLE,
  };
  auto set_display_constraints = dc_client->SetBufferCollectionConstraints(kCollectionId, config);
  CHECK_RSP(set_display_constraints, "Failed to set display constraints");
  if (set_display_constraints->res != ZX_OK) {
    *err_msg_out = "Display constraints error";
    return set_display_constraints->res;
  }

  auto collection = EndpointOrError<sysmem::BufferCollection::SyncClient>::Create();
  CHECK_RSP(collection, "Failed to create collection channel");

  CHECKED_CALL(sysmem_allocator->BindSharedCollection(std::move(*token->mutable_channel()),
                                                      collection.TakeServer()),
               "Failed to bind collection");

  sysmem::BufferCollectionConstraints constraints;
  constraints.usage.cpu = sysmem::cpuUsageWriteOften | sysmem::cpuUsageRead;
  constraints.min_buffer_count = 1;
  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints = image_format::GetDefaultImageFormatConstraints();
  image_constraints.pixel_format.type = sysmem::PixelFormatType::BGRA32;
  image_constraints.pixel_format.has_format_modifier = true;
  image_constraints.pixel_format.format_modifier.value = sysmem::FORMAT_MODIFIER_LINEAR;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0].type = sysmem::ColorSpaceType::SRGB;
  image_constraints.min_coded_width = width;
  image_constraints.min_coded_height = height;
  image_constraints.max_coded_width = 0xffffffff;
  image_constraints.max_coded_height = 0xffffffff;
  image_constraints.min_bytes_per_row = 0;
  image_constraints.max_bytes_per_row = 0xffffffff;

  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = image_format::GetDefaultBufferMemoryConstraints();
  constraints.buffer_memory_constraints.ram_domain_supported = true;

  collection->SetConstraints(true, constraints);
  *collection_client =
      std::make_unique<sysmem::BufferCollection::SyncClient>(std::move(*collection));
  return ZX_OK;
}

// Not static because this function is also called from unit tests.
zx_status_t fb_bind_with_channel(bool single_buffer, const char** err_msg_out,
                                 zx::channel dc_client_channel);

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

  fdio_cpp::FdioCaller caller(std::move(dc_fd));
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
  return fb_bind_with_channel(single_buffer, err_msg_out, std::move(dc_client_channel));
}

zx_status_t fb_bind_with_channel(bool single_buffer, const char** err_msg_out,
                                 zx::channel dc_client_channel) {
  dc_client = std::make_unique<fhd::Controller::SyncClient>(std::move(dc_client_channel));
  auto close_dc_handle = fit::defer([]() {
    zx_handle_close(device_handle);
    dc_client.reset();
    device_handle = ZX_HANDLE_INVALID;
  });

  zx_status_t status;
  zx::channel sysmem_server, sysmem_client;
  status = zx::channel::create(0, &sysmem_server, &sysmem_client);
  if (status != ZX_OK) {
    *err_msg_out = "Failed to create sysmem channel";
    return status;
  }
  status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator", sysmem_server.release());
  if (status != ZX_OK) {
    *err_msg_out = "Failed to connect to sysmem";
    return status;
  }

  sysmem_allocator = std::make_unique<sysmem::Allocator::SyncClient>(std::move(sysmem_client));
  auto close_sysmem_handle = fit::defer([]() { sysmem_allocator.reset(); });

  zx_pixel_format_t pixel_format;
  bool has_display = false;
  fhd::Mode mode;

  fhd::Controller::EventHandlers event_handlers{
      .on_displays_changed =
          [&has_display, &pixel_format,
           &mode](fhd::Controller::OnDisplaysChangedResponse* message) {
            has_display = true;
            display_id = message->added[0].id;
            mode = message->added[0].modes[0];
            pixel_format = message->added[0].pixel_format[0];
            // We're guaranteed that added contains at least one display, since we haven't
            // been notified of any displays to remove.
            return ZX_OK;
          },
      .on_vsync = [](fhd::Controller::OnVsyncResponse* message) { return ZX_ERR_NEXT; },
      .on_client_ownership_change =
          [](fhd::Controller::OnClientOwnershipChangeResponse* message) { return ZX_ERR_NEXT; },
      .unknown = []() { return ZX_ERR_STOP; }};
  do {
    ::fidl::Result result = dc_client->HandleEvents(event_handlers);

    if (!result.ok() && result.status() != ZX_ERR_NEXT) {
      return result.status();
    }
  } while (!has_display);

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
      display_id, fidl::VectorView<uint64_t>(fidl::unowned_ptr(&create_layer_rsp->layer_id), 1));
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

  type_set = false;

  inited = true;

  auto clear_inited = fit::defer([]() { inited = false; });

  zx::vmo local_vmo;

  std::unique_ptr<sysmem::BufferCollection::SyncClient> collection_client;

  status = create_buffer_collection(err_msg_out, &collection_client);
  if (status != ZX_OK) {
    return status;
  }

  auto info_result = collection_client->WaitForBuffersAllocated();
  CHECK_RSP(info_result, "Couldn't wait for fidl buffers allocated");
  if (info_result->status != ZX_OK) {
    *err_msg_out = "Couldn't wait for buffers allocated";
    return info_result->status;
  }
  local_vmo = std::move(info_result->buffer_collection_info.buffers[0].vmo);

  uint32_t bytes_per_row;
  bool got_stride = image_format::GetMinimumRowBytes(
      info_result->buffer_collection_info.settings.image_format_constraints, width, &bytes_per_row);

  if (!got_stride) {
    *err_msg_out = "Couldn't get stride";
    return ZX_ERR_INVALID_ARGS;
  }
  stride = bytes_per_row / ZX_PIXEL_FORMAT_BYTES(pixel_format);

  // Ignore error.
  collection_client->Close();

  // Failure to set the cache policy isn't a fatal error
  zx_vmo_set_cache_policy(local_vmo.get(), ZX_CACHE_POLICY_WRITE_COMBINING);

  uint64_t image_id;
  if ((status = fb_import_image(kCollectionId, 0, IMAGE_TYPE_SIMPLE, &image_id)) != ZX_OK) {
    *err_msg_out = "Couldn't import framebuffer";
    return status;
  }

  if ((status = fb_present_image(image_id, INVALID_ID, INVALID_ID)) != ZX_OK) {
    *err_msg_out = "Failed to present single_buffer mode framebuffer";
    return status;
  }

  in_single_buffer_mode = single_buffer;

  clear_inited.cancel();
  vmo = local_vmo.release();
  close_dc_handle.cancel();
  close_sysmem_handle.cancel();

  return ZX_OK;
}

void fb_release() {
  if (!inited) {
    return;
  }

  dc_client->ReleaseBufferCollection(kCollectionId);

  zx_handle_close(device_handle);
  dc_client.reset();
  sysmem_allocator.reset();
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

zx_status_t fb_import_image(uint64_t collection_id, uint32_t index, uint32_t type,
                            uint64_t* id_out) {
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

  auto import_rsp = dc_client->ImportImage(config, collection_id, index);
  if (!import_rsp.ok()) {
    return import_rsp.status();
  }

  if (import_rsp->res != ZX_OK) {
    return import_rsp->res;
  }

  *id_out = import_rsp->image_id;
  return ZX_OK;
}

zx_status_t fb_present_image(uint64_t image_id, uint64_t wait_event_id, uint64_t signal_event_id) {
  auto rsp = dc_client->SetLayerImage(layer_id, image_id, wait_event_id, signal_event_id);
  if (!rsp.ok()) {
    return rsp.status();
  }

  return dc_client->ApplyConfig().status();
}
