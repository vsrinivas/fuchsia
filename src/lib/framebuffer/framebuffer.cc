// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/framebuffer/framebuffer.h"

#include <fidl/fuchsia.hardware.display/cpp/wire.h>
// FIDL must come before banjo
#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <fuchsia/hardware/display/controller/c/banjo.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fit/defer.h>
#include <lib/image-format-llcpp/image-format-llcpp.h>
#include <lib/image-format/image_format.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/vmo.h>

#include <fbl/unique_fd.h>

#include "src/lib/fsl/handles/object_info.h"

namespace fhd = fuchsia_hardware_display;
namespace sysmem = fuchsia_sysmem;

static std::optional<zx::channel> device_handle;

std::optional<fidl::WireSyncClient<fhd::Controller>> dc_client;
std::optional<fidl::WireSyncClient<sysmem::Allocator>> sysmem_allocator;

static uint64_t display_id;
static uint64_t layer_id;

static int32_t width;
static int32_t height;
static int32_t stride;
static zx_pixel_format_t format;
static bool type_set;
static uint32_t image_type;

static std::optional<zx::vmo> vmo;

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
  fhd::wire::ImageConfig config = {
      .width = width,
      .height = height,
      .pixel_format = format,
      .type = static_cast<uint32_t>(type),
  };
  return dc_client->SetLayerPrimaryConfig(layer_id, config).status();
}

#define CHECKED_CALL(func, err_msg)     \
  {                                     \
    fidl::WireResult rsp = func;        \
    if (!(rsp).ok()) {                  \
      *err_msg_out = err_msg;           \
      return zx::error((rsp).status()); \
    }                                   \
  }

static zx::status<fidl::WireSyncClient<sysmem::BufferCollection>> create_buffer_collection(
    const char** err_msg_out) {
  zx::status token = fidl::CreateEndpoints<sysmem::BufferCollectionToken>();
  if (token.is_error()) {
    *err_msg_out = "Failed to create collection channel";
    return token.take_error();
  }
  CHECKED_CALL(sysmem_allocator->AllocateSharedCollection(std::move(token->server)),
               "Failed to allocate shared collection");
  zx::status display_token = fidl::CreateEndpoints<sysmem::BufferCollectionToken>();
  if (display_token.is_error()) {
    *err_msg_out = "Failed to allocate display token";
    return display_token.take_error();
  }
  CHECKED_CALL(fidl::WireCall(token->client)
                   ->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(display_token->server)),
               "Failed to duplicate token");
  CHECKED_CALL(fidl::WireCall(token->client)->Sync(), "Failed to sync token");

  fidl::WireResult import_rsp =
      dc_client->ImportBufferCollection(kCollectionId, std::move(display_token->client));
  if (!import_rsp.ok()) {
    *err_msg_out = "Failed to import buffer collection";
    return zx::error(import_rsp.status());
  }
  if (import_rsp->res != ZX_OK) {
    *err_msg_out = "Import buffer collection error";
    return zx::error(import_rsp->res);
  }

  fhd::wire::ImageConfig config = {
      .width = static_cast<uint32_t>(width),
      .height = static_cast<uint32_t>(height),
      .pixel_format = format,
      .type = IMAGE_TYPE_SIMPLE,
  };
  fidl::WireResult set_display_constraints =
      dc_client->SetBufferCollectionConstraints(kCollectionId, config);
  if (!set_display_constraints.ok()) {
    *err_msg_out = "Failed to set display constraints";
    return zx::error(set_display_constraints.status());
  }
  if (set_display_constraints->res != ZX_OK) {
    *err_msg_out = "Display constraints error";
    return zx::error(set_display_constraints->res);
  }

  zx::status collection = fidl::CreateEndpoints<sysmem::BufferCollection>();
  if (collection.is_error()) {
    *err_msg_out = "Failed to create collection channel";
    return collection.take_error();
  }

  CHECKED_CALL(sysmem_allocator->BindSharedCollection(std::move(token->client),
                                                      std::move(collection->server)),
               "Failed to bind collection");

  fidl::WireSyncClient client = fidl::BindSyncClient(std::move(collection->client));

  constexpr uint32_t kNamePriority = 1000000;
  const char kNameString[] = "framebuffer";
  CHECKED_CALL(client.SetName(kNamePriority, fidl::StringView(kNameString)),
               "Failed to set framebuffer name");

  sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.cpu = sysmem::wire::kCpuUsageWriteOften | sysmem::wire::kCpuUsageRead;
  constraints.min_buffer_count = 1;
  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints = image_format::GetDefaultImageFormatConstraints();
  image_constraints.pixel_format.type = sysmem::wire::PixelFormatType::kBgra32;
  image_constraints.pixel_format.has_format_modifier = true;
  image_constraints.pixel_format.format_modifier.value = sysmem::wire::kFormatModifierLinear;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0].type = sysmem::wire::ColorSpaceType::kSrgb;
  image_constraints.min_coded_width = width;
  image_constraints.min_coded_height = height;
  image_constraints.max_coded_width = 0xffffffff;
  image_constraints.max_coded_height = 0xffffffff;
  image_constraints.min_bytes_per_row = 0;
  image_constraints.max_bytes_per_row = 0xffffffff;

  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = image_format::GetDefaultBufferMemoryConstraints();
  constraints.buffer_memory_constraints.ram_domain_supported = true;

  client.SetConstraints(true, constraints);
  return zx::ok(std::move(client));
}

// Not static because this function is also called from unit tests.
zx_status_t fb_bind_with(bool single_buffer, const char** err_msg_out,
                         fidl::ClientEnd<fhd::Controller> client);

zx_status_t fb_bind(bool single_buffer, const char** err_msg_out) {
  const char* err_msg;
  if (!err_msg_out) {
    err_msg_out = &err_msg;
  }
  *err_msg_out = "";

  if (inited) {
    *err_msg_out = "framebuffer already initialized";
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

  zx::status dc = fidl::CreateEndpoints<fhd::Controller>();
  if (dc.is_error()) {
    *err_msg_out = "Failed to create controller channel";
    return dc.status_value();
  }

  fdio_cpp::FdioCaller caller(std::move(dc_fd));
  fidl::WireResult open_status =
      fidl::WireCall(caller.borrow_as<fhd::Provider>())
          ->OpenController(std::move(device_server), std::move(dc->server));
  if (open_status.status() != ZX_OK) {
    *err_msg_out = "Failed to call service handle";
    return open_status.status();
  }
  if (status != ZX_OK) {
    *err_msg_out = "Failed to open controller";
    return status;
  }

  device_handle = std::move(device_client);
  return fb_bind_with(single_buffer, err_msg_out, std::move(dc->client));
}

zx_status_t fb_bind_with(bool single_buffer, const char** err_msg_out,
                         fidl::ClientEnd<fhd::Controller> client) {
  dc_client = fidl::BindSyncClient(std::move(client));
  auto close_dc_handle = fit::defer([]() {
    device_handle.reset();
    dc_client.reset();
  });

  zx::status client_end = service::Connect<sysmem::Allocator>();
  if (client_end.is_error()) {
    *err_msg_out = "Failed to connect to sysmem";
    return client_end.status_value();
  }

  sysmem_allocator = fidl::BindSyncClient(std::move(*client_end));
  sysmem_allocator->SetDebugClientInfo(
      fidl::StringView::FromExternal(fsl::GetCurrentProcessName() + "-framebuffer"),
      fsl::GetCurrentProcessKoid());
  auto close_sysmem_handle = fit::defer([]() { sysmem_allocator.reset(); });

  class EventHandler : public fidl::WireSyncEventHandler<fhd::Controller> {
   public:
    EventHandler() = default;

    zx_pixel_format_t pixel_format() const { return pixel_format_; }
    bool has_display() const { return has_display_; }
    fhd::wire::Mode mode() const { return mode_; }

    void OnDisplaysChanged(fidl::WireResponse<fhd::Controller::OnDisplaysChanged>* event) override {
      has_display_ = true;
      // We're guaranteed that added contains at least one display, since we haven't
      // been notified of any displays to remove.
      display_id = event->added[0].id;
      mode_ = event->added[0].modes[0];
      pixel_format_ = event->added[0].pixel_format[0];
    }

    void OnVsync(fidl::WireResponse<fhd::Controller::OnVsync>* event) override {}

    void OnClientOwnershipChange(
        fidl::WireResponse<fhd::Controller::OnClientOwnershipChange>* event) override {}

    zx_status_t Unknown() override { return ZX_ERR_STOP; }

   private:
    zx_pixel_format_t pixel_format_;
    bool has_display_ = false;
    fhd::wire::Mode mode_;
  };

  EventHandler event_handler;
  do {
    ::fidl::Result result = dc_client->HandleOneEvent(event_handler);

    if (!result.ok()) {
      return result.status();
    }
  } while (!event_handler.has_display());

  fidl::WireResult create_layer_rsp = dc_client->CreateLayer();
  if (!create_layer_rsp.ok()) {
    *err_msg_out = "Create layer call failed";
    return create_layer_rsp.status();
  }
  if (create_layer_rsp->res != ZX_OK) {
    *err_msg_out = "Failed to create layer";
    return create_layer_rsp->res;
  }

  fidl::WireResult layers_rsp = dc_client->SetDisplayLayers(
      display_id, fidl::VectorView<uint64_t>::FromExternal(&create_layer_rsp->layer_id, 1));
  if (!layers_rsp.ok()) {
    *err_msg_out = layers_rsp.error().lossy_description();
    return layers_rsp.status();
  }

  if (zx_status_t status =
          set_layer_config(create_layer_rsp->layer_id, event_handler.mode().horizontal_resolution,
                           event_handler.mode().vertical_resolution, event_handler.pixel_format(),
                           IMAGE_TYPE_SIMPLE);
      status != ZX_OK) {
    *err_msg_out = "Failed to set layer config";
    return status;
  }

  layer_id = create_layer_rsp->layer_id;

  width = event_handler.mode().horizontal_resolution;
  height = event_handler.mode().vertical_resolution;
  format = event_handler.pixel_format();

  type_set = false;

  inited = true;

  auto clear_inited = fit::defer([]() { inited = false; });

  zx::vmo local_vmo;

  zx::status collection_client = create_buffer_collection(err_msg_out);
  if (collection_client.is_error()) {
    return collection_client.status_value();
  }

  fidl::WireResult info_result = collection_client->WaitForBuffersAllocated();
  if (!info_result.ok()) {
    *err_msg_out = "Couldn't wait for fidl buffers allocated";
    return info_result.status();
  }
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
  stride = bytes_per_row / ZX_PIXEL_FORMAT_BYTES(event_handler.pixel_format());

  // Ignore error.
  collection_client->Close();

  // Failure to set the cache policy isn't a fatal error
  zx_vmo_set_cache_policy(local_vmo.get(), ZX_CACHE_POLICY_WRITE_COMBINING);

  uint64_t image_id;
  if (zx_status_t status = fb_import_image(kCollectionId, 0, IMAGE_TYPE_SIMPLE, &image_id);
      status != ZX_OK) {
    *err_msg_out = "Couldn't import framebuffer";
    return status;
  }

  if (zx_status_t status = fb_present_image(image_id, INVALID_ID, INVALID_ID); status != ZX_OK) {
    *err_msg_out = "Failed to present single_buffer mode framebuffer";
    return status;
  }

  in_single_buffer_mode = single_buffer;

  clear_inited.cancel();
  vmo = std::move(local_vmo);
  close_dc_handle.cancel();
  close_sysmem_handle.cancel();

  return ZX_OK;
}

void fb_release() {
  if (!inited) {
    return;
  }

  dc_client->ReleaseBufferCollection(kCollectionId);

  device_handle.reset();
  dc_client.reset();
  sysmem_allocator.reset();

  if (in_single_buffer_mode) {
    vmo.reset();
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
  return vmo.value().get();
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

  fhd::wire::ImageConfig config = {
      .width = static_cast<uint32_t>(width),
      .height = static_cast<uint32_t>(height),
      .pixel_format = format,
      .type = type,
  };

  fidl::WireResult import_rsp = dc_client->ImportImage(config, collection_id, index);
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
  fidl::WireResult rsp =
      dc_client->SetLayerImage(layer_id, image_id, wait_event_id, signal_event_id);
  if (!rsp.ok()) {
    return rsp.status();
  }

  return dc_client->ApplyConfig().status();
}
