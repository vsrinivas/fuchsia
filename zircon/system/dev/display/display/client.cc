// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client.h"

#include <fuchsia/hardware/display/c/fidl.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/edid/edid.h>
#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/txn_header.h>
#include <lib/image-format/image_format.h>
#include <lib/zx/channel.h>
#include <math.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include <utility>

#include <ddk/debug.h>
#include <ddk/protocol/display/controller.h>
#include <ddk/trace/event.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>

#define BEGIN_TABLE_CASE if (false) {
#define SELECT_TABLE_CASE(NAME)                                       \
  }                                                                   \
  else if (ordinal == NAME##Ordinal || ordinal == NAME##GenOrdinal) { \
    table = &NAME##RequestTable;
#define HANDLE_REQUEST_CASE(NAME)                                                           \
  }                                                                                         \
  else if (ordinal == fuchsia_hardware_display_Controller##NAME##Ordinal ||                 \
           ordinal == fuchsia_hardware_display_Controller##NAME##GenOrdinal) {              \
    auto req = reinterpret_cast<const fuchsia_hardware_display_Controller##NAME##Request*>( \
        msg.bytes().data());                                                                \
    Handle##NAME(req, &builder, &out_type);
#define END_TABLE_CASE }

namespace {

zx_status_t decode_message(fidl::Message* msg) {
  zx_status_t res;
  const fidl_type_t* table = nullptr;
  // This is an if statement because, depending on the state of the ordinal
  // migration, GenOrdinal and Ordinal may be the same value.  See FIDL-524.
  uint64_t ordinal = msg->ordinal();
  BEGIN_TABLE_CASE
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerImportVmoImage);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerImportImage);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerReleaseImage);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerImportEvent);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerReleaseEvent);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerCreateLayer);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerDestroyLayer);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerSetDisplayMode);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerSetDisplayColorConversion);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerSetDisplayLayers);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerSetLayerPrimaryConfig);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerSetLayerPrimaryPosition);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerSetLayerPrimaryAlpha);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerSetLayerCursorConfig);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerSetLayerCursorPosition);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerSetLayerColorConfig);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerSetLayerImage);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerCheckConfig);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerApplyConfig);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerEnableVsync);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerSetVirtconMode);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerImportBufferCollection);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerSetBufferCollectionConstraints);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerReleaseBufferCollection);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerGetSingleBufferFramebuffer);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerIsCaptureSupported);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerImportImageForCapture);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerStartCapture);
  SELECT_TABLE_CASE(fuchsia_hardware_display_ControllerReleaseCapture);
  END_TABLE_CASE

  if (!table) {
    zxlogf(INFO, "Unknown fidl ordinal %lu\n", ordinal);
    return ZX_ERR_NOT_SUPPORTED;
  }
  const char* err;
  if ((res = msg->Decode(table, &err)) != ZX_OK) {
    zxlogf(INFO, "Error decoding message %lu: %s\n", ordinal, err);
  }
  return res;
}

bool frame_contains(const frame_t& a, const frame_t& b) {
  return b.x_pos < a.width && b.y_pos < a.height && b.x_pos + b.width <= a.width &&
         b.y_pos + b.height <= a.height;
}

// We allocate some variable sized stack allocations based on the number of
// layers, so we limit the total number of layers to prevent blowing the stack.
static constexpr uint64_t kMaxLayers = 65536;

static constexpr uint32_t kInvalidLayerType = UINT32_MAX;

uint32_t calculate_refresh_rate_e2(const edid::timing_params_t params) {
  double total_pxls = (params.horizontal_addressable + params.horizontal_blanking) *
                      (params.vertical_addressable + params.vertical_blanking);
  double pixel_clock_hz = params.pixel_freq_10khz * 1000 * 10;
  return static_cast<uint32_t>(round(100 * pixel_clock_hz / total_pxls));
}

// Removes and invokes EarlyRetire on all entries before end.
static void do_early_retire(list_node_t* list, display::image_node_t* end = nullptr) {
  display::image_node_t* node;
  while ((node = list_peek_head_type(list, display::image_node_t, link)) != end) {
    node->self->EarlyRetire();
    node->self.reset();
    list_remove_head(list);
  }
}

static void populate_image(const fuchsia_hardware_display_ImageConfig& image, image_t* image_out) {
  static_assert(offsetof(image_t, width) == offsetof(fuchsia_hardware_display_ImageConfig, width),
                "Struct mismatch");
  static_assert(offsetof(image_t, height) == offsetof(fuchsia_hardware_display_ImageConfig, height),
                "Struct mismatch");
  static_assert(offsetof(image_t, pixel_format) ==
                    offsetof(fuchsia_hardware_display_ImageConfig, pixel_format),
                "Struct mismatch");
  static_assert(sizeof(image_plane_t) == sizeof(fuchsia_hardware_display_ImagePlane),
                "Struct mismatch");
  static_assert(offsetof(image_t, type) == offsetof(fuchsia_hardware_display_ImageConfig, type),
                "Struct mismatch");
  memcpy(image_out, &image, sizeof(fuchsia_hardware_display_ImageConfig));
}

static void populate_fidl_string(fidl_string_t* dest, fidl::Builder* dest_builder, const char* src,
                                 uint32_t n) {
  dest->data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
  dest->size = strnlen(src, n - 1) + 1;
  char* ptr = dest_builder->NewArray<char>(static_cast<uint32_t>(dest->size));
  snprintf(ptr, dest->size, "%s", src);
}

}  // namespace

namespace display {

void Client::HandleControllerApi(async_dispatcher_t* dispatcher, async::WaitBase* self,
                                 zx_status_t status, const zx_packet_signal_t* signal) {
  if (status == ZX_ERR_CANCELED) {
    zxlogf(INFO, "Wait canceled, client is shutting down\n");
    return;
  } else if (status != ZX_OK) {
    zxlogf(INFO, "Unexpected status async status %d\n", status);
    ZX_DEBUG_ASSERT(false);
    return;
  } else if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    zxlogf(TRACE, "Client closed\n");
    TearDown();
    return;
  }

  ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_READABLE);

  zx_handle_t in_handle;
  uint8_t in_byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
  fidl::Message msg(fidl::BytePart(in_byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES),
                    fidl::HandlePart(&in_handle, 1));
  status = msg.Read(server_handle_, 0);
  api_wait_.Begin(controller_->loop().dispatcher());

  if (status != ZX_OK) {
    zxlogf(TRACE, "Channel read failed %d\n", status);
    return;
  } else if ((status = decode_message(&msg)) != ZX_OK) {
    return;
  }

  uint8_t out_byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
  fidl::Builder builder(out_byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES);
  zx_handle_t out_handle = ZX_HANDLE_INVALID;
  bool has_out_handle = false;
  const fidl_type_t* out_type = nullptr;

  // This is an if statement because, depending on the state of the ordinal
  // migration, GenOrdinal and Ordinal may be the same value.  See FIDL-524.
  uint64_t ordinal = msg.ordinal();
  BEGIN_TABLE_CASE
  HANDLE_REQUEST_CASE(ImportVmoImage);
  HANDLE_REQUEST_CASE(ImportImage);
  HANDLE_REQUEST_CASE(ReleaseImage);
  HANDLE_REQUEST_CASE(ImportEvent);
  HANDLE_REQUEST_CASE(ReleaseEvent);
  HANDLE_REQUEST_CASE(CreateLayer);
  HANDLE_REQUEST_CASE(DestroyLayer);
  HANDLE_REQUEST_CASE(SetDisplayMode);
  HANDLE_REQUEST_CASE(SetDisplayColorConversion);
  HANDLE_REQUEST_CASE(SetDisplayLayers);
  HANDLE_REQUEST_CASE(SetLayerPrimaryConfig);
  HANDLE_REQUEST_CASE(SetLayerPrimaryPosition);
  HANDLE_REQUEST_CASE(SetLayerPrimaryAlpha);
  HANDLE_REQUEST_CASE(SetLayerCursorConfig);
  HANDLE_REQUEST_CASE(SetLayerCursorPosition);
  HANDLE_REQUEST_CASE(SetLayerColorConfig);
  HANDLE_REQUEST_CASE(SetLayerImage);
  HANDLE_REQUEST_CASE(CheckConfig);
  HANDLE_REQUEST_CASE(ApplyConfig);
  HANDLE_REQUEST_CASE(EnableVsync);
  HANDLE_REQUEST_CASE(SetVirtconMode);
  HANDLE_REQUEST_CASE(ImportBufferCollection);
  HANDLE_REQUEST_CASE(ReleaseBufferCollection);
  HANDLE_REQUEST_CASE(SetBufferCollectionConstraints);
  HANDLE_REQUEST_CASE(IsCaptureSupported);
  HANDLE_REQUEST_CASE(ImportImageForCapture);
  HANDLE_REQUEST_CASE(StartCapture);
  HANDLE_REQUEST_CASE(ReleaseCapture);
  END_TABLE_CASE
  else if (ordinal == fuchsia_hardware_display_ControllerGetSingleBufferFramebufferOrdinal ||
           ordinal == fuchsia_hardware_display_ControllerGetSingleBufferFramebufferGenOrdinal) {
    auto r = reinterpret_cast<
        const fuchsia_hardware_display_ControllerGetSingleBufferFramebufferRequest*>(
        msg.bytes().data());
    HandleGetSingleBufferFramebuffer(r, &builder, &out_handle, &has_out_handle, &out_type);
  }
  else {
    zxlogf(INFO, "Unknown ordinal %lu\n", msg.ordinal());
  }

  fidl::BytePart resp_bytes = builder.Finalize();
  if (resp_bytes.actual() != 0) {
    ZX_DEBUG_ASSERT(out_type != nullptr);

    fidl::Message resp(std::move(resp_bytes),
                       fidl::HandlePart(&out_handle, 1, has_out_handle ? 1 : 0));
    resp.header() = msg.header();

    const char* err_msg;
    ZX_DEBUG_ASSERT_MSG(resp.Validate(out_type, &err_msg) == ZX_OK,
                        "Error validating fidl response \"%s\"\n", err_msg);
    if ((status = resp.Write(server_handle_, 0)) != ZX_OK) {
      zxlogf(ERROR, "Error writing response message %d\n", status);
    }
  }
}

void Client::HandleImportVmoImage(
    const fuchsia_hardware_display_ControllerImportVmoImageRequest* req,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto resp = resp_builder->New<fuchsia_hardware_display_ControllerImportVmoImageResponse>();
  *resp_table = &fuchsia_hardware_display_ControllerImportVmoImageResponseTable;
  if (!single_buffer_framebuffer_stride_) {
    resp->res = ZX_ERR_INVALID_ARGS;
    return;
  }

  zx::vmo vmo(req->vmo);

  image_t dc_image;
  dc_image.height = req->image_config.height;
  dc_image.width = req->image_config.width;
  dc_image.pixel_format = req->image_config.pixel_format;
  dc_image.type = req->image_config.type;
  for (uint32_t i = 0; i < fbl::count_of(dc_image.planes); i++) {
    dc_image.planes[i].byte_offset = req->image_config.planes[i].byte_offset;
    dc_image.planes[i].bytes_per_row = req->image_config.planes[i].bytes_per_row;
  }

  zx::vmo dup_vmo;
  resp->res = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
  if (resp->res == ZX_OK) {
    resp->res = controller_->dc()->ImportVmoImage(&dc_image, std::move(dup_vmo), req->offset);
  }

  if (resp->res == ZX_OK) {
    fbl::AllocChecker ac;
    auto image = fbl::AdoptRef(
        new (&ac) Image(controller_, dc_image, std::move(vmo), single_buffer_framebuffer_stride_));
    if (!ac.check()) {
      controller_->dc()->ReleaseImage(&dc_image);

      resp->res = ZX_ERR_NO_MEMORY;
      return;
    }

    image->id = next_image_id_++;
    resp->image_id = image->id;
    images_.insert(std::move(image));
  }
}

void Client::HandleImportImage(const fuchsia_hardware_display_ControllerImportImageRequest* req,
                               fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto resp = resp_builder->New<fuchsia_hardware_display_ControllerImportImageResponse>();
  *resp_table = &fuchsia_hardware_display_ControllerImportImageResponseTable;

  auto it = collection_map_.find(req->collection_id);
  if (it == collection_map_.end()) {
    resp->res = ZX_ERR_INVALID_ARGS;
    return;
  }
  zx::channel& collection = it->second.driver;
  zx_status_t status2;
  zx_status_t status =
      fuchsia_sysmem_BufferCollectionCheckBuffersAllocated(collection.get(), &status2);

  if (status != ZX_OK || status2 != ZX_OK) {
    resp->res = ZX_ERR_SHOULD_WAIT;
    return;
  }

  image_t dc_image = {};
  dc_image.height = req->image_config.height;
  dc_image.width = req->image_config.width;
  dc_image.pixel_format = req->image_config.pixel_format;
  dc_image.type = req->image_config.type;

  resp->res = controller_->dc()->ImportImage(&dc_image, collection.get(), req->index);

  if (resp->res == ZX_OK) {
    auto release_image =
        fbl::MakeAutoCall([this, &dc_image]() { controller_->dc()->ReleaseImage(&dc_image); });
    zx::vmo vmo;
    uint32_t stride = 0;
    if (is_vc_) {
      ZX_ASSERT(it->second.kernel);
      fuchsia_sysmem_BufferCollectionInfo_2 info;
      zx_status_t status2;
      status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(it->second.kernel.get(),
                                                                      &status2, &info);
      if (status != ZX_OK || status2 != ZX_OK) {
        resp->res = ZX_ERR_NO_MEMORY;
        return;
      }
      fbl::Vector<zx::vmo> vmos;
      for (uint32_t i = 0; i < info.buffer_count; ++i) {
        vmos.push_back(zx::vmo(info.buffers[i].vmo));
      }

      if (!info.settings.has_image_format_constraints || req->index >= vmos.size()) {
        resp->res = ZX_ERR_OUT_OF_RANGE;
        return;
      }
      uint32_t minimum_row_bytes;
      if (!ImageFormatMinimumRowBytes(&info.settings.image_format_constraints, dc_image.width,
                                      &minimum_row_bytes)) {
        resp->res = ZX_ERR_INVALID_ARGS;
        return;
      }
      vmo = std::move(vmos[req->index]);
      stride = minimum_row_bytes / ZX_PIXEL_FORMAT_BYTES(dc_image.pixel_format);
    }

    fbl::AllocChecker ac;
    auto image = fbl::AdoptRef(new (&ac) Image(controller_, dc_image, std::move(vmo), stride));
    if (!ac.check()) {
      resp->res = ZX_ERR_NO_MEMORY;
      return;
    }

    image->id = next_image_id_++;
    resp->image_id = image->id;
    release_image.cancel();
    images_.insert(std::move(image));
  }
}

void Client::HandleReleaseImage(const fuchsia_hardware_display_ControllerReleaseImageRequest* req,
                                fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto image = images_.find(req->image_id);
  if (!image.IsValid()) {
    return;
  }

  if (CleanUpImage(&(*image))) {
    ApplyConfig();
  }
}

bool Client::ImportEvent(zx::event event, uint64_t id) {
  fbl::AutoLock lock(&fence_mtx_);
  auto fence = fences_.find(id);
  // Create and ref a new fence.
  if (!fence.IsValid()) {
    // TODO(stevensd): it would be good for this not to be able to fail due to allocation failures
    fbl::AllocChecker ac;
    auto new_fence = fbl::AdoptRef(
        new (&ac) Fence(this, controller_->loop().dispatcher(), id, std::move(event)));
    if (ac.check() && new_fence->CreateRef()) {
      fences_.insert_or_find(std::move(new_fence));
    } else {
      zxlogf(ERROR, "Failed to allocate fence ref for event#%ld\n", id);
      return false;
    }
    return true;
  }

  // Ref an existing fence
  if (fence->event() != event.get()) {
    zxlogf(ERROR, "Cannot reuse event#%ld for zx::event %u\n", id, event.get());
    return false;
  } else if (!fence->CreateRef()) {
    zxlogf(ERROR, "Failed to allocate fence ref for event#%ld\n", id);
    return false;
  }
  return true;
}

void Client::HandleImportEvent(const fuchsia_hardware_display_ControllerImportEventRequest* req,
                               fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  if (req->id == INVALID_ID) {
    zxlogf(ERROR, "Cannot import events with an invalid ID #%i\n", INVALID_ID);
    TearDown();
  } else if (!ImportEvent(zx::event(req->event), req->id)) {
    TearDown();
  }
}

void Client::HandleImportBufferCollection(
    const fuchsia_hardware_display_ControllerImportBufferCollectionRequest* req,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto resp =
      resp_builder->New<fuchsia_hardware_display_ControllerImportBufferCollectionResponse>();
  *resp_table = &fuchsia_hardware_display_ControllerImportBufferCollectionResponseTable;
  zx::channel collection_token(req->collection_token);
  if (!sysmem_allocator_) {
    resp->res = ZX_ERR_NOT_SUPPORTED;
    return;
  }

  // TODO: Switch to .contains() when C++20.
  if (collection_map_.count(req->collection_id)) {
    resp->res = ZX_ERR_INVALID_ARGS;
    return;
  }

  zx::channel vc_collection;

  // Make a second handle to represent the kernel's usage of the buffer as a
  // framebuffer, so we can set constraints and get VMOs for zx_framebuffer_set_range.
  if (is_vc_) {
    zx::channel vc_token_server, vc_token_client;
    zx::channel::create(0, &vc_token_server, &vc_token_client);
    zx_status_t status = fuchsia_sysmem_BufferCollectionTokenDuplicate(
        collection_token.get(), UINT32_MAX, vc_token_server.release());

    if (status != ZX_OK) {
      resp->res = ZX_ERR_INTERNAL;
      return;
    }
    status = fuchsia_sysmem_BufferCollectionTokenSync(collection_token.get());
    if (status != ZX_OK) {
      resp->res = ZX_ERR_INTERNAL;
      return;
    }

    zx::channel collection_server;
    zx::channel::create(0, &collection_server, &vc_collection);
    status = fuchsia_sysmem_AllocatorBindSharedCollection(
        sysmem_allocator_.get(), vc_token_client.release(), collection_server.release());

    if (status != ZX_OK) {
      resp->res = ZX_ERR_INTERNAL;
      return;
    }
  }

  zx::channel collection_server, collection_client;
  zx::channel::create(0, &collection_server, &collection_client);
  zx_status_t status = fuchsia_sysmem_AllocatorBindSharedCollection(
      sysmem_allocator_.get(), collection_token.release(), collection_server.release());

  if (status != ZX_OK) {
    resp->res = ZX_ERR_INTERNAL;
    return;
  }

  collection_map_[req->collection_id] =
      Collections{std::move(collection_client), std::move(vc_collection)};
  resp->res = ZX_OK;
}

void Client::HandleReleaseBufferCollection(
    const fuchsia_hardware_display_ControllerReleaseBufferCollectionRequest* req,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto it = collection_map_.find(req->collection_id);
  if (it == collection_map_.end()) {
    return;
  }

  fuchsia_sysmem_BufferCollectionClose(it->second.driver.get());
  if (it->second.kernel) {
    fuchsia_sysmem_BufferCollectionClose(it->second.kernel.get());
  }
  collection_map_.erase(it);
}

void Client::HandleSetBufferCollectionConstraints(
    const fuchsia_hardware_display_ControllerSetBufferCollectionConstraintsRequest* req,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto resp =
      resp_builder
          ->New<fuchsia_hardware_display_ControllerSetBufferCollectionConstraintsResponse>();
  *resp_table = &fuchsia_hardware_display_ControllerSetBufferCollectionConstraintsResponseTable;
  auto it = collection_map_.find(req->collection_id);
  if (it == collection_map_.end()) {
    resp->res = ZX_ERR_INVALID_ARGS;
    return;
  }
  image_t dc_image;
  dc_image.height = req->config.height;
  dc_image.width = req->config.width;
  dc_image.pixel_format = req->config.pixel_format;
  dc_image.type = req->config.type;
  for (uint32_t i = 0; i < fbl::count_of(dc_image.planes); i++) {
    dc_image.planes[i].byte_offset = req->config.planes[i].byte_offset;
    dc_image.planes[i].bytes_per_row = req->config.planes[i].bytes_per_row;
  }

  resp->res = controller_->dc()->SetBufferCollectionConstraints(&dc_image, it->second.driver.get());

  if (resp->res == ZX_OK && is_vc_) {
    ZX_ASSERT(it->second.kernel);

    // Constraints to be used with zx_framebuffer_set_range.
    fuchsia_sysmem_BufferCollectionConstraints constraints = {};
    constraints.usage.display = fuchsia_sysmem_displayUsageLayer;
    constraints.has_buffer_memory_constraints = true;
    fuchsia_sysmem_BufferMemoryConstraints& buffer_constraints =
        constraints.buffer_memory_constraints;
    buffer_constraints.min_size_bytes = 0;
    buffer_constraints.max_size_bytes = 0xffffffff;
    buffer_constraints.secure_required = false;
    buffer_constraints.ram_domain_supported = true;
    constraints.image_format_constraints_count = 1;
    fuchsia_sysmem_ImageFormatConstraints& image_constraints =
        constraints.image_format_constraints[0];
    switch (req->config.pixel_format) {
      case ZX_PIXEL_FORMAT_RGB_x888:
      case ZX_PIXEL_FORMAT_ARGB_8888:
        image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_BGRA32;
        image_constraints.pixel_format.has_format_modifier = true;
        image_constraints.pixel_format.format_modifier.value =
            fuchsia_sysmem_FORMAT_MODIFIER_LINEAR;
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
    image_constraints.bytes_per_row_divisor = 4;
    image_constraints.start_offset_divisor = 1;
    image_constraints.display_width_divisor = 1;
    image_constraints.display_height_divisor = 1;

    if (image_constraints.pixel_format.type) {
      resp->res = fuchsia_sysmem_BufferCollectionSetConstraints(it->second.kernel.get(), true,
                                                                &constraints);
    }
  }
}

void Client::HandleReleaseEvent(const fuchsia_hardware_display_ControllerReleaseEventRequest* req,
                                fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  // Hold a ref to prevent double locking if this destroys the fence.
  auto fence_ref = GetFence(req->id);
  if (fence_ref) {
    fbl::AutoLock lock(&fence_mtx_);
    fences_.find(req->id)->ClearRef();
  }
}

void Client::HandleCreateLayer(const fuchsia_hardware_display_ControllerCreateLayerRequest* req,
                               fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto resp = resp_builder->New<fuchsia_hardware_display_ControllerCreateLayerResponse>();
  *resp_table = &fuchsia_hardware_display_ControllerCreateLayerResponseTable;

  if (layers_.size() == kMaxLayers) {
    resp->res = ZX_ERR_NO_RESOURCES;
    return;
  }

  fbl::AllocChecker ac;
  auto new_layer = fbl::make_unique_checked<Layer>(&ac);
  if (!ac.check()) {
    resp->res = ZX_ERR_NO_MEMORY;
    return;
  }
  resp->layer_id = next_layer_id++;

  memset(&new_layer->pending_layer_, 0, sizeof(layer_t));
  memset(&new_layer->current_layer_, 0, sizeof(layer_t));
  new_layer->config_change_ = false;
  new_layer->pending_node_.layer = new_layer.get();
  new_layer->current_node_.layer = new_layer.get();
  new_layer->current_display_id_ = INVALID_DISPLAY_ID;
  new_layer->id = resp->layer_id;
  new_layer->current_layer_.type = kInvalidLayerType;
  new_layer->pending_layer_.type = kInvalidLayerType;

  layers_.insert(std::move(new_layer));

  resp->res = ZX_OK;
}

void Client::HandleDestroyLayer(const fuchsia_hardware_display_ControllerDestroyLayerRequest* req,
                                fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto layer = layers_.find(req->layer_id);
  if (!layer.IsValid()) {
    zxlogf(ERROR, "Tried to destroy invalid layer %ld\n", req->layer_id);
    TearDown();
    return;
  }
  if (layer->current_node_.InContainer() || layer->pending_node_.InContainer()) {
    zxlogf(ERROR, "Destroyed layer %ld which was in use\n", req->layer_id);
    TearDown();
    return;
  }

  auto destroyed = layers_.erase(req->layer_id);
  if (destroyed->pending_image_) {
    destroyed->pending_image_->DiscardAcquire();
  }
  do_early_retire(&destroyed->waiting_images_);
  if (destroyed->displayed_image_) {
    fbl::AutoLock lock(controller_->mtx());
    controller_->AssertMtxAliasHeld(destroyed->displayed_image_->mtx());
    destroyed->displayed_image_->StartRetire();
  }
}

void Client::HandleSetDisplayMode(
    const fuchsia_hardware_display_ControllerSetDisplayModeRequest* req,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto config = configs_.find(req->display_id);
  if (!config.IsValid()) {
    return;
  }

  fbl::AutoLock lock(controller_->mtx());
  const fbl::Vector<edid::timing_params_t>* edid_timings;
  const display_params_t* params;
  controller_->GetPanelConfig(req->display_id, &edid_timings, &params);

  if (edid_timings) {
    for (auto timing : *edid_timings) {
      if (timing.horizontal_addressable == req->mode.horizontal_resolution &&
          timing.vertical_addressable == req->mode.vertical_resolution &&
          timing.vertical_refresh_e2 == req->mode.refresh_rate_e2) {
        Controller::PopulateDisplayMode(timing, &config->pending_.mode);
        pending_config_valid_ = false;
        config->display_config_change_ = true;
        return;
      }
    }
    zxlogf(ERROR, "Invalid display mode\n");
  } else {
    zxlogf(ERROR, "Failed to find edid when setting display mode\n");
  }

  TearDown();
}

void Client::HandleSetDisplayColorConversion(
    const fuchsia_hardware_display_ControllerSetDisplayColorConversionRequest* req,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto config = configs_.find(req->display_id);
  if (!config.IsValid()) {
    return;
  }

  config->pending_.cc_flags = 0;
  if (!isnan(req->preoffsets[0])) {
    config->pending_.cc_flags |= COLOR_CONVERSION_PREOFFSET;
    memcpy(config->pending_.cc_preoffsets, req->preoffsets, sizeof(req->preoffsets));
    static_assert(sizeof(req->preoffsets) == sizeof(config->pending_.cc_preoffsets), "");
  }

  if (!isnan(req->coefficients[0])) {
    config->pending_.cc_flags |= COLOR_CONVERSION_COEFFICIENTS;
    memcpy(config->pending_.cc_coefficients, req->coefficients, sizeof(req->coefficients));
    static_assert(sizeof(req->coefficients) == sizeof(config->pending_.cc_coefficients), "");
  }

  if (!isnan(req->postoffsets[0])) {
    config->pending_.cc_flags |= COLOR_CONVERSION_POSTOFFSET;
    memcpy(config->pending_.cc_postoffsets, req->postoffsets, sizeof(req->postoffsets));
    static_assert(sizeof(req->postoffsets) == sizeof(config->pending_.cc_postoffsets), "");
  }

  config->display_config_change_ = true;
  pending_config_valid_ = false;
}

void Client::HandleSetDisplayLayers(
    const fuchsia_hardware_display_ControllerSetDisplayLayersRequest* req,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto config = configs_.find(req->display_id);
  if (!config.IsValid()) {
    return;
  }

  config->pending_layer_change_ = true;
  config->pending_layers_.clear();
  uint64_t* layer_ids = static_cast<uint64_t*>(req->layer_ids.data);
  for (uint64_t i = req->layer_ids.count - 1; i != UINT64_MAX; i--) {
    auto layer = layers_.find(layer_ids[i]);
    if (!layer.IsValid() || layer->pending_node_.InContainer()) {
      zxlogf(ERROR, "Tried to reuse an in-use layer\n");
      TearDown();
      return;
    }
    layer->pending_layer_.z_index = static_cast<uint32_t>(i);
    config->pending_layers_.push_front(&layer->pending_node_);
  }
  config->pending_.layer_count = static_cast<int32_t>(req->layer_ids.count);
  pending_config_valid_ = false;
}

void Client::HandleSetLayerPrimaryConfig(
    const fuchsia_hardware_display_ControllerSetLayerPrimaryConfigRequest* req,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto layer = layers_.find(req->layer_id);
  if (!layer.IsValid()) {
    zxlogf(ERROR, "SetLayerPrimaryConfig on invalid layer\n");
    TearDown();
    return;
  }

  layer->pending_layer_.type = LAYER_TYPE_PRIMARY;
  primary_layer_t* primary_layer = &layer->pending_layer_.cfg.primary;

  populate_image(req->image_config, &primary_layer->image);

  // Initialize the src_frame and dest_frame with the default, full-image frame.
  frame_t new_frame = {
      .x_pos = 0,
      .y_pos = 0,
      .width = req->image_config.width,
      .height = req->image_config.height,
  };
  memcpy(&primary_layer->src_frame, &new_frame, sizeof(frame_t));
  memcpy(&primary_layer->dest_frame, &new_frame, sizeof(frame_t));

  primary_layer->transform_mode = FRAME_TRANSFORM_IDENTITY;

  layer->pending_image_config_gen_++;
  layer->pending_image_ = nullptr;
  layer->config_change_ = true;
  pending_config_valid_ = false;
}

void Client::HandleSetLayerPrimaryPosition(
    const fuchsia_hardware_display_ControllerSetLayerPrimaryPositionRequest* req,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto layer = layers_.find(req->layer_id);
  if (!layer.IsValid() || layer->pending_layer_.type != LAYER_TYPE_PRIMARY) {
    zxlogf(ERROR, "SetLayerPrimaryPosition on invalid layer\n");
    TearDown();
    return;
  }
  if (req->transform > fuchsia_hardware_display_Transform_ROT_90_REFLECT_Y) {
    zxlogf(ERROR, "Invalid transform %d\n", req->transform);
    TearDown();
    return;
  }
  primary_layer_t* primary_layer = &layer->pending_layer_.cfg.primary;

  static_assert(sizeof(fuchsia_hardware_display_Frame) == sizeof(frame_t), "Struct mismatch");
  static_assert(offsetof(fuchsia_hardware_display_Frame, x_pos) == offsetof(frame_t, x_pos),
                "Struct mismatch");
  static_assert(offsetof(fuchsia_hardware_display_Frame, y_pos) == offsetof(frame_t, y_pos),
                "Struct mismatch");
  static_assert(offsetof(fuchsia_hardware_display_Frame, width) == offsetof(frame_t, width),
                "Struct mismatch");
  static_assert(offsetof(fuchsia_hardware_display_Frame, height) == offsetof(frame_t, height),
                "Struct mismatch");

  memcpy(&primary_layer->src_frame, &req->src_frame, sizeof(frame_t));
  memcpy(&primary_layer->dest_frame, &req->dest_frame, sizeof(frame_t));
  primary_layer->transform_mode = static_cast<uint8_t>(req->transform);

  layer->config_change_ = true;
  pending_config_valid_ = false;
}

void Client::HandleSetLayerPrimaryAlpha(
    const fuchsia_hardware_display_ControllerSetLayerPrimaryAlphaRequest* req,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto layer = layers_.find(req->layer_id);
  if (!layer.IsValid() || layer->pending_layer_.type != LAYER_TYPE_PRIMARY) {
    zxlogf(ERROR, "SetLayerPrimaryAlpha on invalid layer\n");
    TearDown();
    return;
  }

  if (req->mode > fuchsia_hardware_display_AlphaMode_HW_MULTIPLY ||
      (!isnan(req->val) && (req->val < 0 || req->val > 1))) {
    zxlogf(ERROR, "Invalid args %d %f\n", req->mode, req->val);
    TearDown();
    return;
  }

  primary_layer_t* primary_layer = &layer->pending_layer_.cfg.primary;

  static_assert(fuchsia_hardware_display_AlphaMode_DISABLE == ALPHA_DISABLE, "Bad constant");
  static_assert(fuchsia_hardware_display_AlphaMode_PREMULTIPLIED == ALPHA_PREMULTIPLIED,
                "Bad constant");
  static_assert(fuchsia_hardware_display_AlphaMode_HW_MULTIPLY == ALPHA_HW_MULTIPLY,
                "Bad constant");

  primary_layer->alpha_mode = req->mode;
  primary_layer->alpha_layer_val = req->val;

  layer->config_change_ = true;
  pending_config_valid_ = false;
}

void Client::HandleSetLayerCursorConfig(
    const fuchsia_hardware_display_ControllerSetLayerCursorConfigRequest* req,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto layer = layers_.find(req->layer_id);
  if (!layer.IsValid()) {
    zxlogf(ERROR, "SetLayerCursorConfig on invalid layer\n");
    TearDown();
    return;
  }

  layer->pending_layer_.type = LAYER_TYPE_CURSOR;
  layer->pending_cursor_x_ = layer->pending_cursor_y_ = 0;

  cursor_layer_t* cursor_layer = &layer->pending_layer_.cfg.cursor;
  populate_image(req->image_config, &cursor_layer->image);

  layer->pending_image_config_gen_++;
  layer->pending_image_ = nullptr;
  layer->config_change_ = true;
  pending_config_valid_ = false;
}

void Client::HandleSetLayerCursorPosition(
    const fuchsia_hardware_display_ControllerSetLayerCursorPositionRequest* req,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto layer = layers_.find(req->layer_id);
  if (!layer.IsValid() || layer->pending_layer_.type != LAYER_TYPE_CURSOR) {
    zxlogf(ERROR, "SetLayerCursorPosition on invalid layer\n");
    TearDown();
    return;
  }

  layer->pending_cursor_x_ = req->x;
  layer->pending_cursor_y_ = req->y;

  layer->config_change_ = true;
}

void Client::HandleSetLayerColorConfig(
    const fuchsia_hardware_display_ControllerSetLayerColorConfigRequest* req,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto layer = layers_.find(req->layer_id);
  if (!layer.IsValid()) {
    zxlogf(ERROR, "SetLayerColorConfig on invalid layer\n");
    return;
  }

  if (req->color_bytes.count != ZX_PIXEL_FORMAT_BYTES(req->pixel_format)) {
    zxlogf(ERROR, "SetLayerColorConfig with invalid color bytes\n");
    TearDown();
    return;
  }
  // Increase the size of the static array when large color formats are introduced
  ZX_ASSERT(req->color_bytes.count <= sizeof(layer->pending_color_bytes_));

  layer->pending_layer_.type = LAYER_TYPE_COLOR;
  color_layer_t* color_layer = &layer->pending_layer_.cfg.color;

  color_layer->format = req->pixel_format;
  memcpy(layer->pending_color_bytes_, req->color_bytes.data, sizeof(layer->pending_color_bytes_));

  layer->pending_image_ = nullptr;
  layer->config_change_ = true;
  pending_config_valid_ = false;
}

void Client::HandleSetLayerImage(const fuchsia_hardware_display_ControllerSetLayerImageRequest* req,
                                 fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto layer = layers_.find(req->layer_id);
  if (!layer.IsValid()) {
    zxlogf(ERROR, "SetLayerImage ordinal with invalid layer\n");
    TearDown();
    return;
  }
  if (layer->pending_layer_.type != LAYER_TYPE_PRIMARY &&
      layer->pending_layer_.type != LAYER_TYPE_CURSOR) {
    zxlogf(ERROR, "SetLayerImage ordinal with bad layer type\n");
    TearDown();
    return;
  }
  auto image = images_.find(req->image_id);
  if (!image.IsValid() || !image->Acquire()) {
    zxlogf(ERROR, "SetLayerImage ordinal with %s image\n", !image.IsValid() ? "invl" : "busy");
    TearDown();
    return;
  }
  image_t* cur_image = layer->pending_layer_.type == LAYER_TYPE_PRIMARY
                           ? &layer->pending_layer_.cfg.primary.image
                           : &layer->pending_layer_.cfg.cursor.image;
  if (!image->HasSameConfig(*cur_image)) {
    zxlogf(ERROR, "SetLayerImage with mismatch layer config\n");
    if (image.IsValid()) {
      image->DiscardAcquire();
    }
    TearDown();
    return;
  }

  if (layer->pending_image_) {
    layer->pending_image_->DiscardAcquire();
  }

  layer->pending_image_ = image.CopyPointer();
  layer->pending_wait_event_id_ = req->wait_event_id;
  layer->pending_signal_event_id_ = req->signal_event_id;
}

void Client::HandleCheckConfig(const fuchsia_hardware_display_ControllerCheckConfigRequest* req,
                               fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  *resp_table = &fuchsia_hardware_display_ControllerCheckConfigResponseTable;

  pending_config_valid_ = CheckConfig(resp_builder);

  if (req->discard) {
    // Go through layers and release any pending resources they claimed
    for (auto& layer : layers_) {
      layer.pending_image_config_gen_ = layer.current_image_config_gen_;
      if (layer.pending_image_) {
        layer.pending_image_->DiscardAcquire();
        layer.pending_image_ = nullptr;
      }
      if (layer.config_change_) {
        layer.pending_layer_ = layer.current_layer_;
        layer.config_change_ = false;

        layer.pending_cursor_x_ = layer.current_cursor_x_;
        layer.pending_cursor_y_ = layer.current_cursor_y_;
      }

      memcpy(layer.pending_color_bytes_, layer.current_color_bytes_,
             sizeof(layer.pending_color_bytes_));
    }
    // Reset each config's pending layers to their current layers. Clear
    // all displays first in case layers were moved between displays.
    for (auto& config : configs_) {
      config.pending_layers_.clear();
    }
    for (auto& config : configs_) {
      fbl::SinglyLinkedList<layer_node_t*> current_layers;
      for (auto& layer_node : config.current_layers_) {
        current_layers.push_front(&layer_node.layer->pending_node_);
      }
      while (!current_layers.is_empty()) {
        auto layer = current_layers.pop_front();
        config.pending_layers_.push_front(layer);
      }
      config.pending_layer_change_ = false;

      config.pending_ = config.current_;
      config.display_config_change_ = false;
    }
    pending_config_valid_ = true;
  }
}

void Client::HandleApplyConfig(const fuchsia_hardware_display_ControllerApplyConfigRequest* req,
                               fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  if (!pending_config_valid_) {
    pending_config_valid_ = CheckConfig(nullptr);
    if (!pending_config_valid_) {
      zxlogf(INFO, "Tried to apply invalid config\n");
      return;
    }
  }

  // First go through and reset any current layer lists that are changing, so
  // we don't end up trying to put an image into two lists.
  for (auto& display_config : configs_) {
    if (display_config.pending_layer_change_) {
      while (!display_config.current_layers_.is_empty()) {
        display_config.current_layers_.pop_front();
      }
    }
  }

  for (auto& display_config : configs_) {
    if (display_config.display_config_change_) {
      display_config.current_ = display_config.pending_;
      display_config.display_config_change_ = false;
    }

    // Update any image layers. This needs to be done before migrating layers, as
    // that needs to know if there are any waiting images.
    for (auto layer_node : display_config.pending_layers_) {
      Layer* layer = layer_node.layer;
      // If the layer's image configuration changed, get rid of any current images
      if (layer->pending_image_config_gen_ != layer->current_image_config_gen_) {
        layer->current_image_config_gen_ = layer->pending_image_config_gen_;

        if (layer->pending_image_ == nullptr) {
          zxlogf(ERROR, "Tried to apply configuration with missing image\n");
          TearDown();
          return;
        }

        while (!list_is_empty(&layer->waiting_images_)) {
          do_early_retire(&layer->waiting_images_);
        }
        if (layer->displayed_image_ != nullptr) {
          {
            fbl::AutoLock lock(controller_->mtx());
            controller_->AssertMtxAliasHeld(layer->displayed_image_->mtx());
            layer->displayed_image_->StartRetire();
          }
          layer->displayed_image_ = nullptr;
        }
      }

      if (layer->pending_image_) {
        auto wait_fence = GetFence(layer->pending_wait_event_id_);
        if (wait_fence && wait_fence->InContainer()) {
          zxlogf(ERROR, "Tried to wait with a busy event\n");
          TearDown();
          return;
        }
        layer_node.layer->pending_image_->PrepareFences(std::move(wait_fence),
                                                        GetFence(layer->pending_signal_event_id_));
        {
          fbl::AutoLock lock(controller_->mtx());
          controller_->AssertMtxAliasHeld(layer->pending_image_->mtx());
          list_add_tail(&layer->waiting_images_, &layer->pending_image_->node.link);
          layer->pending_image_->node.self = std::move(layer->pending_image_);
        }
      }
    }

    // If there was a layer change, update the current layers list.
    if (display_config.pending_layer_change_) {
      fbl::SinglyLinkedList<layer_node_t*> new_current;
      for (auto layer_node : display_config.pending_layers_) {
        new_current.push_front(&layer_node.layer->current_node_);
      }

      while (!new_current.is_empty()) {
        // Don't migrate images between displays if there are pending images. See
        // Controller::ApplyConfig for more details.
        auto* layer = new_current.pop_front();
        if (layer->layer->current_display_id_ != display_config.id &&
            layer->layer->displayed_image_ && !list_is_empty(&layer->layer->waiting_images_)) {
          {
            fbl::AutoLock lock(controller_->mtx());
            controller_->AssertMtxAliasHeld(layer->layer->displayed_image_->mtx());
            layer->layer->displayed_image_->StartRetire();
          }
          layer->layer->displayed_image_ = nullptr;

          // This doesn't need to be reset anywhere, since we really care about the last
          // display this layer was shown on. Ignoring the 'null' display could cause
          // unusual layer changes to trigger this unnecessary, but that's not wrong.
          layer->layer->current_display_id_ = display_config.id;
        }
        layer->layer->current_layer_.z_index = layer->layer->pending_layer_.z_index;

        display_config.current_layers_.push_front(layer);
      }
      display_config.pending_layer_change_ = false;
      display_config.pending_apply_layer_change_ = true;
    }

    // Apply any pending configuration changes to active layers.
    for (auto layer_node : display_config.current_layers_) {
      Layer* layer = layer_node.layer;
      if (layer->config_change_) {
        layer->current_layer_ = layer->pending_layer_;
        layer->config_change_ = false;

        image_t* new_image_config = nullptr;
        if (layer->current_layer_.type == LAYER_TYPE_PRIMARY) {
          new_image_config = &layer->current_layer_.cfg.primary.image;
        } else if (layer->current_layer_.type == LAYER_TYPE_CURSOR) {
          new_image_config = &layer->current_layer_.cfg.cursor.image;

          layer->current_cursor_x_ = layer->pending_cursor_x_;
          layer->current_cursor_y_ = layer->pending_cursor_y_;

          display_mode_t* mode = &display_config.current_.mode;
          layer->current_layer_.cfg.cursor.x_pos = fbl::clamp(
              layer->current_cursor_x_, -static_cast<int32_t>(new_image_config->width) + 1,
              static_cast<int32_t>(mode->h_addressable) - 1);
          layer->current_layer_.cfg.cursor.y_pos = fbl::clamp(
              layer->current_cursor_y_, -static_cast<int32_t>(new_image_config->height) + 1,
              static_cast<int32_t>(mode->v_addressable) - 1);
        } else if (layer->current_layer_.type == LAYER_TYPE_COLOR) {
          memcpy(layer->current_color_bytes_, layer->pending_color_bytes_,
                 sizeof(layer->current_color_bytes_));
          layer->current_layer_.cfg.color.color_list = layer->current_color_bytes_;
          layer->current_layer_.cfg.color.color_count = 4;
        } else {
          // type is validated in ::CheckConfig, so something must be very wrong.
          ZX_ASSERT(false);
        }

        if (new_image_config && layer->displayed_image_) {
          new_image_config->handle = layer->displayed_image_->info().handle;
        }
      }
    }
  }
  // Overflow doesn't matter, since stamps only need to be unique until
  // the configuration is applied with vsync.
  client_apply_count_++;

  ApplyConfig();
}

void Client::HandleEnableVsync(const fuchsia_hardware_display_ControllerEnableVsyncRequest* req,
                               fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  proxy_->EnableVsync(req->enable);
}

void Client::HandleSetVirtconMode(
    const fuchsia_hardware_display_ControllerSetVirtconModeRequest* req,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  if (!is_vc_) {
    zxlogf(ERROR, "Illegal non-virtcon ownership\n");
    TearDown();
    return;
  }
  controller_->SetVcMode(req->mode);
}

void Client::HandleGetSingleBufferFramebuffer(
    const fuchsia_hardware_display_ControllerGetSingleBufferFramebufferRequest* req,
    fidl::Builder* resp_builder, zx_handle_t* handle_out, bool* has_handle_out,
    const fidl_type_t** resp_table) {
  auto resp =
      resp_builder->New<fuchsia_hardware_display_ControllerGetSingleBufferFramebufferResponse>();
  *resp_table = &fuchsia_hardware_display_ControllerGetSingleBufferFramebufferResponseTable;

  zx::vmo vmo;
  uint32_t stride = 0;
  resp->res = controller_->dc()->GetSingleBufferFramebuffer(&vmo, &stride);
  *has_handle_out = resp->res == ZX_OK;
  *handle_out = vmo.release();
  resp->vmo = *has_handle_out ? FIDL_HANDLE_PRESENT : FIDL_HANDLE_ABSENT;
  resp->stride = stride;
  single_buffer_framebuffer_stride_ = stride;
}

void Client::HandleIsCaptureSupported(
    const fuchsia_hardware_display_ControllerIsCaptureSupportedRequest* /*req*/,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto resp = resp_builder->New<fuchsia_hardware_display_ControllerIsCaptureSupportedResponse>();
  *resp_table = &fuchsia_hardware_display_ControllerIsCaptureSupportedResponseTable;
  resp->result.response.supported = controller_->dc_capture() != nullptr;
}

void Client::HandleImportImageForCapture(
    const fuchsia_hardware_display_ControllerImportImageForCaptureRequest* req,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto resp = resp_builder->New<fuchsia_hardware_display_ControllerImportImageForCaptureResponse>();
  *resp_table = &fuchsia_hardware_display_ControllerImportImageForCaptureResponseTable;

  // Ensure display driver supports/implements capture.
  if (controller_->dc_capture() == nullptr) {
    resp->result.tag = fuchsia_hardware_display_Controller_ImportImageForCapture_ResultTag_err;
    resp->result.err = ZX_ERR_NOT_SUPPORTED;
    return;
  }

  // Ensure a previously imported collection id is being used for import.
  auto it = collection_map_.find(req->collection_id);
  if (it == collection_map_.end()) {
    resp->result.tag = fuchsia_hardware_display_Controller_ImportImageForCapture_ResultTag_err;
    resp->result.err = ZX_ERR_INVALID_ARGS;
    return;
  }

  // Check whether buffer has already been allocated for the requested collection id.
  zx::channel& collection = it->second.driver;
  zx_status_t status2;
  zx_status_t status =
      fuchsia_sysmem_BufferCollectionCheckBuffersAllocated(collection.get(), &status2);
  if (status != ZX_OK || status2 != ZX_OK) {
    resp->result.tag = fuchsia_hardware_display_Controller_ImportImageForCapture_ResultTag_err;
    resp->result.err = ZX_ERR_SHOULD_WAIT;
    return;
  }

  // capture_image will contain a handle that will be used by display driver to trigger
  // capture start/release.
  image_t capture_image = {};
  status = controller_->dc_capture()->ImportImageForCapture(collection.get(), req->index,
                                                            &capture_image.handle);
  if (status == ZX_OK) {
    auto release_image = fbl::MakeAutoCall([this, &capture_image]() {
      controller_->dc_capture()->ReleaseCapture(capture_image.handle);
    });

    fbl::AllocChecker ac;
    auto image = fbl::AdoptRef(new (&ac) Image(controller_, capture_image));
    if (!ac.check()) {
      resp->result.tag = fuchsia_hardware_display_Controller_ImportImageForCapture_ResultTag_err;
      resp->result.err = ZX_ERR_NO_MEMORY;
      return;
    }
    image->id = next_capture_image_id++;
    resp->result.response.image_id = image->id;
    release_image.cancel();
    capture_images_.insert(std::move(image));
  } else {
    resp->result.tag = fuchsia_hardware_display_Controller_ImportImageForCapture_ResultTag_err;
    resp->result.err = status;
    return;
  }
}

void Client::HandleStartCapture(const fuchsia_hardware_display_ControllerStartCaptureRequest* req,
                                fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto resp = resp_builder->New<fuchsia_hardware_display_ControllerStartCaptureResponse>();
  *resp_table = &fuchsia_hardware_display_ControllerStartCaptureResponseTable;

  // Ensure display driver supports/implements capture.
  if (controller_->dc_capture() == nullptr) {
    resp->result.tag = fuchsia_hardware_display_Controller_StartCapture_ResultTag_err;
    resp->result.err = ZX_ERR_NOT_SUPPORTED;
    return;
  }

  // Don't start capture if one is in progress
  if (current_capture_image_ != INVALID_ID) {
    resp->result.tag = fuchsia_hardware_display_Controller_StartCapture_ResultTag_err;
    resp->result.err = ZX_ERR_SHOULD_WAIT;
    return;
  }

  // Ensure we have a capture fence for the request signal event.
  auto signal_fence = GetFence(req->signal_event_id);
  if (signal_fence == nullptr) {
    resp->result.tag = fuchsia_hardware_display_Controller_StartCapture_ResultTag_err;
    resp->result.err = ZX_ERR_INVALID_ARGS;
    return;
  }

  // Ensure we are capturing into a valid image buffer
  auto image = capture_images_.find(req->image_id);
  if (!image.IsValid()) {
    zxlogf(ERROR, "Invalid Capture Image ID requested for capture\n");
    resp->result.tag = fuchsia_hardware_display_Controller_StartCapture_ResultTag_err;
    resp->result.err = ZX_ERR_INVALID_ARGS;
    return;
  }

  capture_fence_id_ = req->signal_event_id;
  auto status = controller_->dc_capture()->StartCapture(image->info().handle);
  if (status == ZX_OK) {
    fbl::AutoLock lock(controller_->mtx());
    proxy_->EnableCapture(true);
  } else {
    resp->result.tag = fuchsia_hardware_display_Controller_StartCapture_ResultTag_err;
    resp->result.err = status;
  }

  // keep track of currently active capture image
  current_capture_image_ = req->image_id;
}

void Client::HandleReleaseCapture(
    const fuchsia_hardware_display_ControllerReleaseCaptureRequest* req,
    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
  auto resp = resp_builder->New<fuchsia_hardware_display_ControllerReleaseCaptureResponse>();
  *resp_table = &fuchsia_hardware_display_ControllerReleaseCaptureResponseTable;

  // Ensure display driver supports/implements capture
  if (controller_->dc_capture() == nullptr) {
    resp->result.tag = fuchsia_hardware_display_Controller_ReleaseCapture_ResultTag_err;
    resp->result.err = ZX_ERR_NOT_SUPPORTED;
    return;
  }

  // Ensure we are releasing a valid image buffer
  auto image = capture_images_.find(req->image_id);
  if (!image.IsValid()) {
    zxlogf(ERROR, "Invalid Capture Image ID requested for release\n");
    resp->result.tag = fuchsia_hardware_display_Controller_ReleaseCapture_ResultTag_err;
    resp->result.err = ZX_ERR_INVALID_ARGS;
    return;
  }

  // Make sure we are not releasing an active capture.
  if (current_capture_image_ == req->image_id) {
    // we have an active capture. Release it when capture is completed
    zxlogf(WARN, "Capture is active. Will release after capture is complete\n");
    pending_capture_release_image_ = current_capture_image_;
  } else {
    // release image now
    capture_images_.erase(image);
  }
}

bool Client::CheckConfig(fidl::Builder* resp_builder) {
  const display_config_t* configs[configs_.size()];
  layer_t* layers[layers_.size()];
  uint32_t layer_cfg_results[layers_.size()];
  uint32_t* display_layer_cfg_results[configs_.size()];
  memset(layer_cfg_results, 0, layers_.size() * sizeof(uint32_t));

  fuchsia_hardware_display_ControllerCheckConfigResponse* resp = nullptr;
  if (resp_builder) {
    resp = resp_builder->New<fuchsia_hardware_display_ControllerCheckConfigResponse>();
    resp->res = fuchsia_hardware_display_ConfigResult_OK;
    resp->ops.count = 0;
    resp->ops.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
  }

  bool config_fail = false;
  int config_idx = 0;
  int layer_idx = 0;
  for (auto& display_config : configs_) {
    if (display_config.pending_layers_.is_empty()) {
      continue;
    }

    // Put this display's display_config_t* into the compact array
    configs[config_idx] = &display_config.pending_;

    // Set the index in the primary result array with this display's layer result array
    display_layer_cfg_results[config_idx++] = layer_cfg_results + layer_idx;

    // Create this display's compact layer_t* array
    display_config.pending_.layer_list = layers + layer_idx;

    // Frame used for checking that each layer's dest_frame lies entirely
    // within the composed output.
    frame_t display_frame = {
        .x_pos = 0,
        .y_pos = 0,
        .width = display_config.pending_.mode.h_addressable,
        .height = display_config.pending_.mode.v_addressable,
    };

    // Do any work that needs to be done to make sure that the pending layer_t structs
    // are up to date, and validate that the configuration doesn't violate any API
    // constraints.
    for (auto& layer_node : display_config.pending_layers_) {
      layers[layer_idx++] = &layer_node.layer->pending_layer_;

      bool invalid = false;
      if (layer_node.layer->pending_layer_.type == LAYER_TYPE_PRIMARY) {
        primary_layer_t* layer = &layer_node.layer->pending_layer_.cfg.primary;
        // Frame for checking that the layer's src_frame lies entirely
        // within the source image.
        frame_t image_frame = {
            .x_pos = 0,
            .y_pos = 0,
            .width = layer->image.width,
            .height = layer->image.height,
        };
        invalid = (!frame_contains(image_frame, layer->src_frame) ||
                   !frame_contains(display_frame, layer->dest_frame));

        if (!invalid) {
          invalid = true;
          for (auto fmt : display_config.pixel_formats_) {
            if (fmt == layer->image.pixel_format) {
              invalid = false;
              break;
            }
          }
        }
      } else if (layer_node.layer->pending_layer_.type == LAYER_TYPE_CURSOR) {
        invalid = true;
        auto& cursor_cfg = layer_node.layer->pending_layer_.cfg.cursor;
        for (auto& cursor_info : display_config.cursor_infos_) {
          if (cursor_info.format == cursor_cfg.image.pixel_format) {
            invalid = false;
            break;
          }
        }
      } else if (layer_node.layer->pending_layer_.type == LAYER_TYPE_COLOR) {
        // There aren't any API constraints on valid colors.
        layer_node.layer->pending_layer_.cfg.color.color_list =
            layer_node.layer->pending_color_bytes_;
        layer_node.layer->pending_layer_.cfg.color.color_count = 4;
      } else {
        invalid = true;
      }

      if (invalid) {
        // Continue to the next display, since there's nothing more to check for this one.
        config_fail = true;
        break;
      }
    }
  }

  if (config_fail) {
    if (resp) {
      resp->res = fuchsia_hardware_display_ConfigResult_INVALID_CONFIG;
    }
    // If the config is invalid, there's no point in sending it to the impl driver.
    return false;
  }

  size_t layer_cfg_results_count;
  uint32_t display_cfg_result = controller_->dc()->CheckConfiguration(
      configs, config_idx, display_layer_cfg_results, &layer_cfg_results_count);

  if (display_cfg_result != CONFIG_DISPLAY_OK) {
    if (resp) {
      resp->res = display_cfg_result == CONFIG_DISPLAY_TOO_MANY
                      ? fuchsia_hardware_display_ConfigResult_TOO_MANY_DISPLAYS
                      : fuchsia_hardware_display_ConfigResult_UNSUPPORTED_DISPLAY_MODES;
    }
    return false;
  }

  bool layer_fail = false;
  for (int i = 0; i < config_idx && !layer_fail; i++) {
    for (unsigned j = 0; j < configs[i]->layer_count && !layer_fail; j++) {
      if (display_layer_cfg_results[i][j]) {
        layer_fail = true;
      }
    }
  }

  // Return unless we need to finish constructing the response
  if (!layer_fail) {
    return true;
  } else if (!resp_builder) {
    return false;
  }
  resp->res = fuchsia_hardware_display_ConfigResult_UNSUPPORTED_CONFIG;

  static_assert((1 << fuchsia_hardware_display_ClientCompositionOpcode_CLIENT_USE_PRIMARY) ==
                    CLIENT_USE_PRIMARY,
                "Const mismatch");
  static_assert((1 << fuchsia_hardware_display_ClientCompositionOpcode_CLIENT_MERGE_BASE) ==
                    CLIENT_MERGE_BASE,
                "Const mismatch");
  static_assert(
      (1 << fuchsia_hardware_display_ClientCompositionOpcode_CLIENT_MERGE_SRC) == CLIENT_MERGE_SRC,
      "Const mismatch");
  static_assert((1 << fuchsia_hardware_display_ClientCompositionOpcode_CLIENT_FRAME_SCALE) ==
                    CLIENT_FRAME_SCALE,
                "Const mismatch");
  static_assert(
      (1 << fuchsia_hardware_display_ClientCompositionOpcode_CLIENT_SRC_FRAME) == CLIENT_SRC_FRAME,
      "Const mismatch");
  static_assert(
      (1 << fuchsia_hardware_display_ClientCompositionOpcode_CLIENT_TRANSFORM) == CLIENT_TRANSFORM,
      "Const mismatch");
  static_assert((1 << fuchsia_hardware_display_ClientCompositionOpcode_CLIENT_COLOR_CONVERSION) ==
                    CLIENT_COLOR_CONVERSION,
                "Const mismatch");
  static_assert(
      (1 << fuchsia_hardware_display_ClientCompositionOpcode_CLIENT_ALPHA) == CLIENT_ALPHA,
      "Const mismatch");
  constexpr uint32_t kAllErrors = (CLIENT_ALPHA << 1) - 1;

  layer_idx = 0;
  for (auto& display_config : configs_) {
    if (display_config.pending_layers_.is_empty()) {
      continue;
    }

    bool seen_base = false;
    for (auto& layer_node : display_config.pending_layers_) {
      uint32_t err = kAllErrors & layer_cfg_results[layer_idx];
      // Fixup the error flags if the driver impl incorrectly set multiple MERGE_BASEs
      if (err & CLIENT_MERGE_BASE) {
        if (seen_base) {
          err &= ~CLIENT_MERGE_BASE;
          err |= CLIENT_MERGE_SRC;
        } else {
          seen_base = true;
          err &= ~CLIENT_MERGE_SRC;
        }
      }

      for (uint8_t i = 0; i < 32; i++) {
        if (err & (1 << i)) {
          auto op = resp_builder->New<fuchsia_hardware_display_ClientCompositionOp>();
          op->display_id = display_config.id;
          op->layer_id = layer_node.layer->id;
          op->opcode = i;
          resp->ops.count++;
        }
      }
      layer_idx++;
    }
  }
  return false;
}

void Client::ApplyConfig() {
  ZX_DEBUG_ASSERT(controller_->current_thread_is_loop());
  TRACE_DURATION("gfx", "Display::Client::ApplyConfig");

  bool config_missing_image = false;
  layer_t* layers[layers_.size()];
  int layer_idx = 0;
  for (auto& display_config : configs_) {
    display_config.current_.layer_count = 0;
    display_config.current_.layer_list = layers + layer_idx;
    display_config.vsync_layer_count_ = 0;

    // Displays with no current layers are filtered out in Controller::ApplyConfig,
    // after it updates its own image tracking logic.

    for (auto layer_node : display_config.current_layers_) {
      // Find the newest image which has become ready
      Layer* layer = layer_node.layer;
      image_node_t* node = list_peek_tail_type(&layer->waiting_images_, image_node_t, link);
      while (node != nullptr && !node->self->IsReady()) {
        node = list_prev_type(&layer->waiting_images_, &node->link, image_node_t, link);
      }
      if (node != nullptr) {
        if (layer->displayed_image_ != nullptr) {
          // Start retiring the image which had been displayed
          fbl::AutoLock lock(controller_->mtx());
          controller_->AssertMtxAliasHeld(layer->displayed_image_->mtx());
          layer->displayed_image_->StartRetire();
        } else {
          // Turning on a new layer is a (pseudo) layer change
          display_config.pending_apply_layer_change_ = true;
        }

        // Drop any images older than node.
        do_early_retire(&layer->waiting_images_, node);

        layer->displayed_image_ = std::move(node->self);
        list_remove_head(&layer->waiting_images_);

        uint64_t handle = layer->displayed_image_->info().handle;
        if (layer->current_layer_.type == LAYER_TYPE_PRIMARY) {
          layer->current_layer_.cfg.primary.image.handle = handle;
        } else if (layer->current_layer_.type == LAYER_TYPE_CURSOR) {
          layer->current_layer_.cfg.cursor.image.handle = handle;
        } else {
          // type is validated in ::CheckConfig, so something must be very wrong.
          ZX_ASSERT(false);
        }
      }

      if (is_vc_) {
        if (layer->displayed_image_) {
          // If the virtcon is displaying an image, set it as the kernel's framebuffer
          // vmo. If the virtcon is displaying images on multiple displays, this ends
          // executing multiple times, but the extra work is okay since the virtcon
          // shouldn't be flipping images.
          console_fb_display_id_ = display_config.id;

          auto fb = layer->displayed_image_;
          uint32_t stride = fb->stride_px();
          uint32_t size =
              fb->info().height * ZX_PIXEL_FORMAT_BYTES(fb->info().pixel_format) * stride;
          // Please do not use get_root_resource() in new code. See ZX-1467.
          zx_framebuffer_set_range(get_root_resource(), fb->vmo().get(), size,
                                   fb->info().pixel_format, fb->info().width, fb->info().height,
                                   stride);
        } else if (console_fb_display_id_ == display_config.id) {
          // If this display doesnt' have an image but it was the display which had the
          // kernel's framebuffer, make the kernel drop the reference. Note that this
          // executes when tearing down the virtcon client.
          // Please do not use get_root_resource() in new code. See ZX-1467.
          zx_framebuffer_set_range(get_root_resource(), ZX_HANDLE_INVALID, 0, 0, 0, 0, 0);
          console_fb_display_id_ = -1;
        }
      }

      display_config.current_.layer_count++;
      layers[layer_idx++] = &layer->current_layer_;
      if (layer->current_layer_.type != LAYER_TYPE_COLOR) {
        display_config.vsync_layer_count_++;
        if (layer->displayed_image_ == nullptr) {
          config_missing_image = true;
        }
      }
    }
  }

  if (!config_missing_image && is_owner_) {
    DisplayConfig* dc_configs[configs_.size()];
    int dc_idx = 0;
    for (auto& c : configs_) {
      dc_configs[dc_idx++] = &c;
    }
    controller_->ApplyConfig(dc_configs, dc_idx, is_vc_, client_apply_count_, id_);
  }
}

void Client::SetOwnership(bool is_owner) {
  ZX_DEBUG_ASSERT(controller_->current_thread_is_loop());

  is_owner_ = is_owner;

  fuchsia_hardware_display_ControllerClientOwnershipChangeEvent msg = {};
  fidl_init_txn_header(&msg.hdr, 0,
                       fuchsia_hardware_display_ControllerClientOwnershipChangeGenOrdinal);
  msg.has_ownership = is_owner;

  zx_status_t status = zx_channel_write(server_handle_, 0, &msg, sizeof(msg), nullptr, 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Error writing remove message %d\n", status);
  }

  ApplyConfig();
}

void Client::OnDisplaysChanged(const uint64_t* displays_added, size_t added_count,
                               const uint64_t* displays_removed, size_t removed_count) {
  ZX_DEBUG_ASSERT(controller_->current_thread_is_loop());
  controller_->AssertMtxAliasHeld(controller_->mtx());

  uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  fidl::Builder builder(bytes, ZX_CHANNEL_MAX_MSG_BYTES);
  auto req = builder.New<fuchsia_hardware_display_ControllerDisplaysChangedEvent>();
  zx_status_t status;
  fidl_init_txn_header(&req->hdr, 0, fuchsia_hardware_display_ControllerDisplaysChangedGenOrdinal);
  req->added.count = 0;
  req->added.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
  req->removed.count = 0;
  req->removed.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);

  for (unsigned i = 0; i < removed_count; i++) {
    // TODO(stevensd): Delayed removal can cause conflicts if the driver reuses
    // display ids. Move display id generation into the core driver.
    if (configs_.find(displays_removed[i]).IsValid()) {
      req->removed.count++;
    }
  }

  for (unsigned i = 0; i < added_count; i++) {
    fbl::AllocChecker ac;
    auto config = fbl::make_unique_checked<DisplayConfig>(&ac);
    if (!ac.check()) {
      zxlogf(WARN, "Out of memory when processing hotplug\n");
      continue;
    }

    config->id = displays_added[i];

    if (!controller_->GetSupportedPixelFormats(config->id, &config->pixel_formats_)) {
      zxlogf(WARN, "Failed to get pixel formats when processing hotplug\n");
      continue;
    }

    if (!controller_->GetCursorInfo(config->id, &config->cursor_infos_)) {
      zxlogf(WARN, "Failed to get cursor info when processing hotplug\n");
      continue;
    }

    const fbl::Vector<edid::timing_params_t>* edid_timings;
    const display_params_t* params;
    if (!controller_->GetPanelConfig(config->id, &edid_timings, &params)) {
      // This can only happen if the display was already disconnected.
      zxlogf(WARN, "No config when adding display\n");
      continue;
    }
    req->added.count++;

    config->current_.display_id = config->id;
    config->current_.layer_list = nullptr;
    config->current_.layer_count = 0;

    if (edid_timings) {
      Controller::PopulateDisplayMode((*edid_timings)[0], &config->current_.mode);
    } else {
      config->current_.mode = {};
      config->current_.mode.h_addressable = params->width;
      config->current_.mode.v_addressable = params->height;
    }

    config->current_.cc_flags = 0;

    config->pending_ = config->current_;

    configs_.insert(std::move(config));
  }

  // We need 2 loops, since we need to make sure we allocate the
  // correct size array in the fidl response.
  fuchsia_hardware_display_Info* coded_configs = nullptr;
  if (req->added.count > 0) {
    coded_configs =
        builder.NewArray<fuchsia_hardware_display_Info>(static_cast<uint32_t>(req->added.count));
  }

  for (unsigned i = 0; i < added_count; i++) {
    auto config = configs_.find(displays_added[i]);
    if (!config.IsValid()) {
      continue;
    }

    const fbl::Vector<edid::timing_params>* edid_timings;
    const display_params_t* params;
    controller_->GetPanelConfig(config->id, &edid_timings, &params);

    coded_configs[i].id = config->id;
    coded_configs[i].pixel_format.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
    coded_configs[i].modes.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
    coded_configs[i].cursor_configs.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);

    if (edid_timings) {
      coded_configs[i].modes.count = edid_timings->size();
      for (auto timing : *edid_timings) {
        auto mode = builder.New<fuchsia_hardware_display_Mode>();

        mode->horizontal_resolution = timing.horizontal_addressable;
        mode->vertical_resolution = timing.vertical_addressable;
        mode->refresh_rate_e2 = timing.vertical_refresh_e2;
      }
    } else {
      coded_configs[i].modes.count = 1;
      auto mode = builder.New<fuchsia_hardware_display_Mode>();
      mode->horizontal_resolution = params->width;
      mode->vertical_resolution = params->height;
      mode->refresh_rate_e2 = params->refresh_rate_e2;
    }

    static_assert(sizeof(zx_pixel_format_t) == sizeof(int32_t), "Bad pixel format size");
    coded_configs[i].pixel_format.count = config->pixel_formats_.size();
    memcpy(
        builder.NewArray<zx_pixel_format_t>(static_cast<uint32_t>(config->pixel_formats_.size())),
        config->pixel_formats_.data(), sizeof(zx_pixel_format_t) * config->pixel_formats_.size());

    static_assert(
        offsetof(cursor_info_t, width) == offsetof(fuchsia_hardware_display_CursorInfo, width),
        "Bad struct");
    static_assert(
        offsetof(cursor_info_t, height) == offsetof(fuchsia_hardware_display_CursorInfo, height),
        "Bad struct");
    static_assert(offsetof(cursor_info_t, format) ==
                      offsetof(fuchsia_hardware_display_CursorInfo, pixel_format),
                  "Bad struct");
    static_assert(sizeof(cursor_info_t) <= sizeof(fuchsia_hardware_display_CursorInfo), "Bad size");
    coded_configs[i].cursor_configs.count = config->cursor_infos_.size();
    auto coded_cursor_configs = builder.NewArray<fuchsia_hardware_display_CursorInfo>(
        static_cast<uint32_t>(config->cursor_infos_.size()));
    for (unsigned i = 0; i < config->cursor_infos_.size(); i++) {
      memcpy(&coded_cursor_configs[i], &config->cursor_infos_[i], sizeof(cursor_info_t));
    }

    const char* manufacturer_name = "";
    const char* monitor_name = "";
    const char* monitor_serial = "";
    if (!controller_->GetDisplayIdentifiers(displays_added[i], &manufacturer_name, &monitor_name,
                                            &monitor_serial)) {
      zxlogf(ERROR, "Failed to get display identifiers\n");
      ZX_DEBUG_ASSERT(false);
    }

    populate_fidl_string(&coded_configs[i].manufacturer_name, &builder, manufacturer_name,
                         fuchsia_hardware_display_identifierMaxLen);
    populate_fidl_string(&coded_configs[i].monitor_name, &builder, monitor_name,
                         fuchsia_hardware_display_identifierMaxLen);
    populate_fidl_string(&coded_configs[i].monitor_serial, &builder, monitor_serial,
                         fuchsia_hardware_display_identifierMaxLen);
  }

  if (req->removed.count > 0) {
    auto removed_ids = builder.NewArray<uint64_t>(static_cast<uint32_t>(req->removed.count));
    uint32_t idx = 0;
    for (unsigned i = 0; i < removed_count; i++) {
      auto display = configs_.erase(displays_removed[i]);
      if (display) {
        display->pending_layers_.clear();
        display->current_layers_.clear();
        removed_ids[idx++] = display->id;
      }
    }
  }

  if (req->added.count > 0 || req->removed.count > 0) {
    fidl::Message msg(builder.Finalize(), fidl::HandlePart());
    const char* err;
    ZX_DEBUG_ASSERT_MSG(
        msg.Validate(&fuchsia_hardware_display_ControllerDisplaysChangedEventTable, &err) == ZX_OK,
        "Failed to validate \"%s\"", err);

    if ((status = msg.Write(server_handle_, 0)) != ZX_OK) {
      zxlogf(ERROR, "Error writing remove message %d\n", status);
    }
  }
}

fbl::RefPtr<FenceReference> Client::GetFence(uint64_t id) {
  if (id == INVALID_ID) {
    return nullptr;
  }
  fbl::AutoLock lock(&fence_mtx_);
  auto fence = fences_.find(id);
  return fence.IsValid() ? fence->GetReference() : nullptr;
}

void Client::OnFenceFired(FenceReference* fence) {
  for (auto& layer : layers_) {
    image_node_t* waiting;
    list_for_every_entry (&layer.waiting_images_, waiting, image_node_t, link) {
      waiting->self->OnFenceReady(fence);
    }
  }
  ApplyConfig();
}

void Client::OnRefForFenceDead(Fence* fence) {
  fbl::AutoLock lock(&fence_mtx_);
  if (fence->OnRefDead()) {
    fences_.erase(fence->id);
  }
}

void Client::CaptureCompleted() {
  auto signal_fence = GetFence(capture_fence_id_);
  if (signal_fence != nullptr) {
    signal_fence->Signal();
  }
  proxy_->EnableCapture(false);

  // release any pending capture images
  if (pending_capture_release_image_ == current_capture_image_) {
    auto image = capture_images_.find(pending_capture_release_image_);
    if (image.IsValid()) {
      capture_images_.erase(image);
    }
    pending_capture_release_image_ = INVALID_ID;
  }
  current_capture_image_ = INVALID_ID;
}

void Client::TearDown() {
  ZX_DEBUG_ASSERT(controller_->current_thread_is_loop());
  pending_config_valid_ = false;

  // Teardown stops events from the channel, but not from the ddk, so we
  // need to make sure we don't try to teardown multiple times.
  if (!IsValid()) {
    return;
  }

  server_handle_ = ZX_HANDLE_INVALID;
  if (api_wait_.object() != ZX_HANDLE_INVALID) {
    api_wait_.Cancel();
    api_wait_.set_object(ZX_HANDLE_INVALID);
  }

  CleanUpImage(nullptr);
  CleanUpCaptureImage();

  // Use a temporary list to prevent double locking when resetting
  fbl::SinglyLinkedList<fbl::RefPtr<Fence>> fences;
  {
    fbl::AutoLock lock(&fence_mtx_);
    while (!fences_.is_empty()) {
      fences.push_front(fences_.erase(fences_.begin()));
    }
  }
  while (!fences.is_empty()) {
    fences.pop_front()->ClearRef();
  }

  for (auto& config : configs_) {
    config.pending_layers_.clear();
    config.current_layers_.clear();
  }

  // The layer's images have already been handled in CleanUpImageLayerState
  layers_.clear();

  ApplyConfig();

  proxy_->OnClientDead();
}

void Client::TearDownTest() { server_handle_ = ZX_HANDLE_INVALID; }

bool Client::CleanUpImage(Image* image) {
  // Clean up any fences associated with the image
  {
    fbl::AutoLock lock(controller_->mtx());
    if (image) {
      controller_->AssertMtxAliasHeld(image->mtx());
      image->ResetFences();
    } else {
      for (auto& image : images_) {
        controller_->AssertMtxAliasHeld(image.mtx());
        image.ResetFences();
      }
    }
  }

  // Clean up any layer state associated with the images
  bool current_config_change = false;
  for (auto& layer : layers_) {
    if (layer.pending_image_ && (image == nullptr || layer.pending_image_.get() == image)) {
      layer.pending_image_->DiscardAcquire();
      layer.pending_image_ = nullptr;
    }
    if (image == nullptr) {
      do_early_retire(&layer.waiting_images_, nullptr);
    } else {
      image_node_t* waiting;
      list_for_every_entry (&layer.waiting_images_, waiting, image_node_t, link) {
        if (waiting->self.get() == image) {
          list_delete(&waiting->link);
          waiting->self->EarlyRetire();
          waiting->self.reset();
          break;
        }
      }
    }
    if (layer.displayed_image_ && (image == nullptr || layer.displayed_image_.get() == image)) {
      {
        fbl::AutoLock lock(controller_->mtx());
        controller_->AssertMtxAliasHeld(layer.displayed_image_->mtx());
        layer.displayed_image_->StartRetire();
      }
      layer.displayed_image_ = nullptr;

      if (layer.current_node_.InContainer()) {
        current_config_change = true;
      }
    }
  }

  // Clean up the image id map
  if (image) {
    images_.erase(*image);
  } else {
    images_.clear();
  }

  return current_config_change;
}

void Client::CleanUpCaptureImage() {
  if (current_capture_image_ != INVALID_ID) {
    // There is an active capture. Need to wait for that to stop before
    // releasing the resources
    // 200ms should be plenty of time for capture to complete
    int64_t timeout = 200;  // unit in ms
    while (!controller_->dc_capture()->IsCaptureCompleted() && timeout--) {
      zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
    }
    // Timeout is fatal since capture never completed and hardware is in unknown state
    ZX_ASSERT(timeout > 0);
    auto image = capture_images_.find(current_capture_image_);
    if (image.IsValid()) {
      capture_images_.erase(image);
    }
  }
}

zx_status_t Client::Init(zx_handle_t server_handle) {
  zx_status_t status;

  api_wait_.set_object(server_handle);
  api_wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
  if ((status = api_wait_.Begin(controller_->loop().dispatcher())) != ZX_OK) {
    // Clear the object, since that's used to detect whether or not api_wait_ is inited.
    api_wait_.set_object(ZX_HANDLE_INVALID);
    zxlogf(ERROR, "Failed to start waiting %d\n", status);
    return status;
  }

  server_handle_ = server_handle;

  zx::channel sysmem_allocator_request;
  zx::channel::create(0, &sysmem_allocator_request, &sysmem_allocator_);
  status = controller_->dc()->GetSysmemConnection(std::move(sysmem_allocator_request));
  if (status != ZX_OK) {
    // Not a fatal error, but BufferCollection functions won't work.
    // TODO(ZX-3355) TODO: Fail creation once all drivers implement this.
    zxlogf(ERROR, "GetSysmemConnection failed (continuing) - status: %d\n", status);
    sysmem_allocator_.reset();
  }

  mtx_init(&fence_mtx_, mtx_plain);

  return ZX_OK;
}

Client::Client(Controller* controller, ClientProxy* proxy, bool is_vc, uint32_t client_id)
    : controller_(controller), proxy_(proxy), is_vc_(is_vc), id_(client_id) {}

Client::Client(Controller* controller, ClientProxy* proxy, bool is_vc, uint32_t client_id,
               zx_handle_t server_handle)
    : controller_(controller),
      proxy_(proxy),
      is_vc_(is_vc),
      id_(client_id),
      server_handle_(server_handle) {}

Client::~Client() { ZX_DEBUG_ASSERT(server_handle_ == ZX_HANDLE_INVALID); }

void ClientProxy::SetOwnership(bool is_owner) {
  auto task = new async::Task();
  task->set_handler([client_handler = &handler_, is_owner](async_dispatcher_t* dispatcher,
                                                           async::Task* task, zx_status_t status) {
    if (status == ZX_OK && client_handler->IsValid()) {
      client_handler->SetOwnership(is_owner);
    }

    delete task;
  });
  task->Post(controller_->loop().dispatcher());
}

void ClientProxy::OnDisplaysChanged(const uint64_t* displays_added, size_t added_count,
                                    const uint64_t* displays_removed, size_t removed_count) {
  handler_.OnDisplaysChanged(displays_added, added_count, displays_removed, removed_count);
}

void ClientProxy::ReapplyConfig() {
  fbl::AllocChecker ac;
  auto task = new (&ac) async::Task();
  if (!ac.check()) {
    zxlogf(WARN, "Failed to reapply config\n");
    return;
  }

  task->set_handler([client_handler = &handler_](async_dispatcher_t* dispatcher, async::Task* task,
                                                 zx_status_t status) {
    if (status == ZX_OK && client_handler->IsValid()) {
      client_handler->ApplyConfig();
    }

    delete task;
  });
  task->Post(controller_->loop().dispatcher());
}

zx_status_t ClientProxy::OnCaptureComplete() {
  ZX_DEBUG_ASSERT(mtx_trylock(controller_->mtx()) == thrd_busy);
  if (enable_capture_) {
    handler_.CaptureCompleted();
  }
  return ZX_OK;
}

zx_status_t ClientProxy::OnDisplayVsync(uint64_t display_id, zx_time_t timestamp,
                                        uint64_t* image_ids, size_t count) {
  ZX_DEBUG_ASSERT(mtx_trylock(controller_->mtx()) == thrd_busy);

  if (!enable_vsync_) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  uint32_t size = static_cast<uint32_t>(sizeof(fuchsia_hardware_display_ControllerVsyncEvent) +
                                        sizeof(uint64_t) * count);
  uint8_t data[size];

  fuchsia_hardware_display_ControllerVsyncEvent* msg =
      reinterpret_cast<fuchsia_hardware_display_ControllerVsyncEvent*>(data);
  fidl_init_txn_header(&msg->hdr, 0, fuchsia_hardware_display_ControllerVsyncGenOrdinal);
  msg->display_id = display_id;
  msg->timestamp = timestamp;
  msg->images.count = count;
  msg->images.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);

  memcpy(msg + 1, image_ids, sizeof(uint64_t) * count);

  zx_status_t status = server_channel_.write(0, data, size, nullptr, 0);
  if (status != ZX_OK) {
    if (status == ZX_ERR_NO_MEMORY) {
      total_oom_errors_++;
      // OOM errors are most likely not recoverable. Print the error message
      // once every kChannelErrorPrintFreq cycles
      if (chn_oom_print_freq_++ == 0) {
        zxlogf(ERROR, "Failed to send vsync event (OOM) (total occurrences: %lu)\n",
               total_oom_errors_);
      }
      if (chn_oom_print_freq_ >= kChannelOomPrintFreq) {
        chn_oom_print_freq_ = 0;
      }
    } else {
      zxlogf(WARN, "Failed to send vsync event %d\n", status);
    }
  }

  return status;
}

void ClientProxy::OnClientDead() {
  controller_->OnClientDead(this);
  // After OnClientDead, there won't be any more vsync calls. Since that is the only use of
  // the channel off of the loop thread, there's no need to worry about synchronization.
  server_channel_.reset();
}

void ClientProxy::CloseTest() { handler_.TearDownTest(); }

void ClientProxy::CloseOnControllerLoop() {
  mtx_t mtx;
  mtx_init(&mtx, mtx_plain);
  cnd_t cnd;
  cnd_init(&cnd);
  bool done = false;
  mtx_lock(&mtx);
  auto task = new async::Task();
  task->set_handler([client_handler = &handler_, cnd_ptr = &cnd, mtx_ptr = &mtx, done_ptr = &done](
                        async_dispatcher_t* dispatcher, async::Task* task, zx_status_t status) {
    mtx_lock(mtx_ptr);

    client_handler->TearDown();

    *done_ptr = true;
    cnd_signal(cnd_ptr);
    mtx_unlock(mtx_ptr);

    delete task;
  });
  if (task->Post(controller_->loop().dispatcher()) != ZX_OK) {
    // Tasks only fail to post if the looper is dead. That can happen if the controller is unbinding
    // and shutting down active clients, but if it does then it's safe to call Reset on this thread
    // anyway.
    delete task;
    handler_.TearDown();
  } else {
    while (!done) {
      cnd_wait(&cnd, &mtx);
    }
  }
  mtx_unlock(&mtx);
}

zx_status_t ClientProxy::DdkClose(uint32_t flags) {
  zxlogf(INFO, "DdkClose\n");
  CloseOnControllerLoop();
  return ZX_OK;
}

void ClientProxy::DdkUnbindNew(ddk::UnbindTxn txn) {
  zxlogf(INFO, "ClientProxy::DdkUnbind\n");
  CloseOnControllerLoop();
  txn.Reply();
}

void ClientProxy::DdkRelease() { delete this; }

zx_status_t ClientProxy::Init(zx::channel server_channel) {
  server_channel_ = std::move(server_channel);
  return handler_.Init(server_channel_.get());
}

ClientProxy::ClientProxy(Controller* controller, bool is_vc, uint32_t client_id)
    : ClientParent(controller->zxdev()),
      controller_(controller),
      is_vc_(is_vc),
      handler_(controller_, this, is_vc_, client_id) {}

ClientProxy::ClientProxy(Controller* controller, bool is_vc, uint32_t client_id,
                         zx::channel server_channel)
    : ClientParent(nullptr),
      controller_(controller),
      is_vc_(is_vc),
      server_channel_(std::move(server_channel)),
      handler_(controller_, this, is_vc_, server_channel_.get(), client_id) {}

ClientProxy::~ClientProxy() {}

}  // namespace display
