// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vc-display.h"

#include <fcntl.h>
#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/fidl/coding.h>
#include <lib/image-format-llcpp/image-format-llcpp.h>
#include <lib/statusor/endpoint-or-error.h>
#include <lib/statusor/status-macros.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <ddk/protocol/display/controller.h>
#include <fbl/unique_fd.h>

#include "vc.h"

namespace fhd = ::llcpp::fuchsia::hardware::display;
namespace sysmem = ::llcpp::fuchsia::sysmem;

static async_dispatcher_t* dc_dispatcher = nullptr;
// At any point, |dc_wait| will either be waiting on the display controller device directory
// for a display controller instance or it will be waiting on a display controller interface
// for messages.
static async::Wait dc_wait;

static std::unique_ptr<fhd::Controller::SyncClient> dc_client;

static std::unique_ptr<sysmem::Allocator::SyncClient> sysmem_allocator;
static uint64_t next_buffer_collection_id = 1;

static struct list_node display_list = LIST_INITIAL_VALUE(display_list);
static bool primary_bound = false;

// remember whether the virtual console controls the display
bool g_vc_owns_display = false;

static void vc_find_display_controller();

bool is_primary_bound() { return primary_bound; }

#if BUILD_FOR_DISPLAY_TEST

struct list_node* get_display_list() {
  return &display_list;
}

sysmem::Allocator::SyncClient* get_sysmem_allocator() { return sysmem_allocator.get(); }

#endif  // BUILD_FOR_DISPLAY_TEST

static constexpr const char* kDisplayControllerDir = "/dev/class/display-controller";
static int dc_dir_fd;
static zx_handle_t dc_device;

static zx_status_t vc_set_mode(fhd::VirtconMode mode) {
  return dc_client->SetVirtconMode(static_cast<uint8_t>(mode)).status();
}

void vc_attach_to_main_display(vc_t* vc) {
  if (list_is_empty(&display_list)) {
    return;
  }
  display_info_t* primary = list_peek_head_type(&display_list, display_info_t, node);
  vc->graphics = primary->graphics;
  vc_attach_gfx(vc);
}

void vc_toggle_framebuffer() {
  if (list_is_empty(&display_list)) {
    return;
  }

  zx_status_t status =
      vc_set_mode(!g_vc_owns_display ? fhd::VirtconMode::FORCED : fhd::VirtconMode::FALLBACK);
  if (status != ZX_OK) {
    printf("vc: Failed to toggle ownership %d\n", status);
  }
}

static void handle_ownership_change(bool has_ownership) {
  g_vc_owns_display = has_ownership;

#if !BUILD_FOR_DISPLAY_TEST
  // If we've gained it, repaint
  if (g_vc_owns_display && g_active_vc) {
    vc_full_repaint(g_active_vc);
    vc_render(g_active_vc);
  }
#endif  // !BUILD_FOR_DISPLAY_TEST
}

zx_status_t create_layer(uint64_t display_id, uint64_t* layer_id) {
  auto rsp = dc_client->CreateLayer();
  RETURN_IF_ERROR(rsp, "vc: Create layer call failed");

  if (rsp->res != ZX_OK) {
    printf("vc: Failed to create layer %d\n", rsp->res);
    return rsp->res;
  }

  *layer_id = rsp->layer_id;
  return ZX_OK;
}

void destroy_layer(uint64_t layer_id) {
  if (!dc_client->DestroyLayer(layer_id).ok()) {
    printf("vc: Failed to destroy layer\n");
  }
}

void release_image(uint64_t image_id) {
  if (!dc_client->ReleaseImage(image_id).ok()) {
    printf("vc: Failed to release image\n");
  }
}

static zx_status_t handle_display_added(fhd::Info* info) {
  display_info_t* display_info =
      reinterpret_cast<display_info_t*>(calloc(1, sizeof(display_info_t)));
  if (!display_info) {
    printf("vc: failed to alloc display info\n");
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status;
  if ((status = create_layer(info->id, &display_info->layer_id)) != ZX_OK) {
    printf("vc: failed to create display layer\n");
    free(display_info);
    return status;
  }

  display_info->id = info->id;
  display_info->width = info->modes[0].horizontal_resolution;
  display_info->height = info->modes[0].vertical_resolution;
  display_info->format = static_cast<int32_t>(info->pixel_format[0]);
  display_info->image_id = 0;
  display_info->image_vmo = ZX_HANDLE_INVALID;
  display_info->buffer_collection_id = 0;
  display_info->bound = false;
  display_info->log_vc = nullptr;
  display_info->graphics = nullptr;

  list_add_tail(&display_list, &display_info->node);

  return ZX_OK;
}

void handle_display_removed(uint64_t id) {
  if (list_is_empty(&display_list)) {
    printf("vc: No displays when removing %ld\n", id);
    return;
  }

  bool was_primary = list_peek_head_type(&display_list, display_info_t, node)->id == id;
  display_info_t* info = nullptr;
  display_info_t* temp = nullptr;
  list_for_every_entry_safe (&display_list, info, temp, display_info_t, node) {
    if (info->id == id) {
      destroy_layer(info->layer_id);
      release_image(info->image_id);
      list_delete(&info->node);
      zx_handle_close(info->image_vmo);
      if (info->buffer_collection_id) {
        dc_client->ReleaseBufferCollection(info->buffer_collection_id);
      }

      if (info->graphics) {
        free(info->graphics);
      }
      if (info->log_vc) {
        log_delete_vc(info->log_vc);
      }
      free(info);
    }
  }

  if (was_primary) {
    set_log_listener_active(false);
    primary_bound = false;
  }
}

static zx_status_t get_single_framebuffer(zx_handle_t* vmo_out, uint32_t* stride_out) {
  auto rsp = dc_client->GetSingleBufferFramebuffer();
  RETURN_IF_ERROR(rsp, "vc: Failed to get single framebuffer");
  if (rsp->res != ZX_OK) {
    // Don't print an error since this can happen on non-single-framebuffer
    // systems.
    return rsp->res;
  }
  if (!rsp->vmo) {
    return ZX_ERR_INTERNAL;
  }

  *vmo_out = rsp->vmo.release();
  *stride_out = rsp->stride;
  return ZX_OK;
}

zx_status_t import_buffer_collection(uint64_t collection_id, fhd::ImageConfig* config,
                                     uint64_t* id) {
  constexpr uint32_t kImageIndex = 0;
  auto import_rsp = dc_client->ImportImage(*config, collection_id, kImageIndex);
  RETURN_IF_ERROR(import_rsp, "vc: Failed to import image call");

  if (import_rsp->res != ZX_OK) {
    printf("vc: Failed to import vmo collection %d\n", import_rsp->res);
    return import_rsp->res;
  }

  *id = import_rsp->image_id;
  return ZX_OK;
}

zx_status_t import_vmo(zx_handle_t vmo, fhd::ImageConfig* config, uint64_t* id) {
  zx_handle_t vmo_dup;
  zx_status_t status;
  if ((status = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &vmo_dup)) != ZX_OK) {
    printf("vc: Failed to dup fb handle %d\n", status);
    return status;
  }

  auto import_rsp = dc_client->ImportVmoImage(*config, zx::vmo(vmo_dup), 0);
  RETURN_IF_ERROR(import_rsp, "vc: Failed to import vmo call");

  if (import_rsp->res != ZX_OK) {
    printf("vc: Failed to import vmo %d\n", import_rsp->res);
    return import_rsp->res;
  }

  *id = import_rsp->image_id;
  return ZX_OK;
}

zx_status_t set_display_layer(uint64_t display_id, uint64_t layer_id) {
  RETURN_IF_ERROR(
      dc_client->SetDisplayLayers(
          display_id, fidl::VectorView<uint64_t>(fidl::unowned_ptr(&layer_id), layer_id ? 1 : 0)),
      "vc: Failed to set display layers");
  return ZX_OK;
}

zx_status_t configure_layer(display_info_t* display, uint64_t layer_id, uint64_t image_id,
                            fhd::ImageConfig* config) {
  RETURN_IF_ERROR(dc_client->SetLayerPrimaryConfig(layer_id, *config),
                  "vc: Failed to set layer config");
  RETURN_IF_ERROR(dc_client->SetLayerPrimaryPosition(
                      layer_id, fhd::Transform::IDENTITY,
                      fhd::Frame{.width = config->width, .height = config->height},
                      fhd::Frame{.width = display->width, .height = display->height}),
                  "vc: Failed to set layer position");
  RETURN_IF_ERROR(dc_client->SetLayerImage(layer_id, image_id, 0, 0), "vc: Failed to set image");
  return ZX_OK;
}

zx_status_t apply_configuration() {
  // Validate and then apply the new configuration
  auto check_rsp = dc_client->CheckConfig(false);
  RETURN_IF_ERROR(check_rsp, "vc: Failed to validate display config");

  if (check_rsp->res != fhd::ConfigResult::OK) {
    printf("vc: Config not valid %d\n", static_cast<int>(check_rsp->res));
    return ZX_ERR_INTERNAL;
  }

  RETURN_IF_ERROR(dc_client->ApplyConfig(), "Applying config failed");

  return ZX_OK;
}

static zx_status_t create_buffer_collection(
    display_info_t* display, uint64_t id,
    std::unique_ptr<sysmem::BufferCollection::SyncClient>* collection_client) {
  ASSIGN_OR_RETURN(auto token, EndpointOrError<sysmem::BufferCollectionToken::SyncClient>::Create(),
                   "vc: Failed to create collection channel");
  RETURN_IF_ERROR(sysmem_allocator->AllocateSharedCollection(token.TakeServer()),
                  "vc: Failed to allocate shared collection");
  ASSIGN_OR_RETURN(auto display_token,
                   EndpointOrError<sysmem::BufferCollectionToken::SyncClient>::Create(),
                   "vc: Failed to allocate display token");
  RETURN_IF_ERROR(token->Duplicate(ZX_RIGHT_SAME_RIGHTS, display_token.TakeServer()),
                  "vc: Failed to duplicate token");
  ASSIGN_OR_RETURN(auto collection, EndpointOrError<sysmem::BufferCollection::SyncClient>::Create(),
                   "vc: Failed to create collection channel");
  RETURN_IF_ERROR(sysmem_allocator->BindSharedCollection(std::move(*token->mutable_channel()),
                                                         collection.TakeServer()),
                  "vc: Failed to bind collection");
  RETURN_IF_ERROR(collection->Sync(), "vc: Failed to sync collection");

  auto import_rsp =
      dc_client->ImportBufferCollection(id, std::move(*display_token->mutable_channel()));
  RETURN_IF_ERROR(import_rsp, "vc: Failed to import buffer collection");
  if (import_rsp->res != ZX_OK) {
    printf("vc: Import buffer collection error\n");
    return import_rsp->res;
  }

  auto set_display_constraints =
      dc_client->SetBufferCollectionConstraints(id, display->image_config);
  RETURN_IF_ERROR(set_display_constraints, "vc: Failed to set display constraints");
  if (set_display_constraints->res != ZX_OK) {
    printf("vc: Display constraints error\n");
    return set_display_constraints->res;
  }

  sysmem::BufferCollectionConstraints constraints;
  constraints.usage.cpu = sysmem::cpuUsageWriteOften | sysmem::cpuUsageRead;
  constraints.min_buffer_count = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = image_format::GetDefaultBufferMemoryConstraints();
  constraints.buffer_memory_constraints.ram_domain_supported = true;

  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints = image_format::GetDefaultImageFormatConstraints();
  fuchsia_sysmem_PixelFormat pixel_format;
  if (!ImageFormatConvertZxToSysmem(display->format, &pixel_format)) {
    printf("vc: Unsupported pixel format");
    return ZX_ERR_INVALID_ARGS;
  }
  image_constraints.pixel_format = image_format::GetCppPixelFormat(pixel_format);
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0].type = sysmem::ColorSpaceType::SRGB;
  image_constraints.min_coded_width = display->width;
  image_constraints.min_coded_height = display->height;
  image_constraints.max_coded_width = 0xffffffff;
  image_constraints.max_coded_height = 0xffffffff;
  image_constraints.min_bytes_per_row = 0;
  image_constraints.max_bytes_per_row = 0xffffffff;

  RETURN_IF_ERROR(collection->SetConstraints(true, constraints), "vc: Failed to set constraints");
  *collection_client =
      std::make_unique<sysmem::BufferCollection::SyncClient>(std::move(*collection));
  return ZX_OK;
}

zx_status_t alloc_display_info_vmo(display_info_t* display) {
  display->buffer_collection_id = 0;
  display->image_config.height = display->height;
  display->image_config.width = display->width;
  display->image_config.pixel_format = display->format;
  display->image_config.type = IMAGE_TYPE_SIMPLE;
  if (get_single_framebuffer(&display->image_vmo, &display->stride) != ZX_OK) {
    uint64_t buffer_collection_id = next_buffer_collection_id++;
    std::unique_ptr<sysmem::BufferCollection::SyncClient> collection_client;
    zx_status_t status =
        create_buffer_collection(display, buffer_collection_id, &collection_client);
    if (status != ZX_OK) {
      return status;
    }
    display->buffer_collection_id = buffer_collection_id;

    auto info_result = collection_client->WaitForBuffersAllocated();
    RETURN_IF_ERROR(info_result, "vc: Couldn't wait for buffers allocated");
    if (info_result->status != ZX_OK) {
      printf("vc: Couldn't wait for buffers allocated\n");
      return info_result->status;
    }

    uint32_t bytes_per_row;
    bool got_stride = image_format::GetMinimumRowBytes(
        info_result->buffer_collection_info.settings.image_format_constraints, display->width,
        &bytes_per_row);

    if (!got_stride) {
      return ZX_ERR_INVALID_ARGS;
    }
    display->stride = bytes_per_row / ZX_PIXEL_FORMAT_BYTES(display->format);
    display->image_vmo = info_result->buffer_collection_info.buffers[0].vmo.release();
    collection_client->Close();
  }
  return ZX_OK;
}

zx_status_t rebind_display(bool use_all) {
  // Arbitrarily pick the oldest display as the primary dispay
  display_info* primary = list_peek_head_type(&display_list, display_info, node);
  if (primary == nullptr) {
    printf("vc: No display to bind to\n");
    return ZX_ERR_NO_RESOURCES;
  }

  zx_status_t status;
  // This happens when the last primary disconnected and a new, already
  // bound display becomes primary. We must un-bind the display and
  // rebind.
  if (!primary_bound && primary->bound) {
    // Remove the primary display's log console.
    if (primary->log_vc) {
      log_delete_vc(primary->log_vc);
      primary->log_vc = nullptr;
    }
    // Switch all of the current vcs to using this display.
    vc_change_graphics(primary->graphics);
  }

  display_info_t* info = nullptr;
  list_for_every_entry (&display_list, info, display_info_t, node) {
    if (!use_all && info != primary) {
      // If we're not showing anything on this display, remove its layer.
      if ((status = set_display_layer(info->id, 0)) != ZX_OK) {
        break;
      }
    } else if (info->image_id == 0) {
      // If we want to display something but aren't, configure the display.
      if ((status = alloc_display_info_vmo(info)) != ZX_OK) {
        printf("vc: failed to allocate vmo for new display %d\n", status);
        break;
      }

      info->graphics = reinterpret_cast<vc_gfx_t*>(calloc(1, sizeof(vc_gfx_t)));
      if ((status = vc_init_gfx(info->graphics, info->image_vmo, info->width, info->height,
                                info->format, info->stride)) != ZX_OK) {
        printf("vc: failed to initialize graphics for new display %d\n", status);
        break;
      }

      // If this is not the primary display then create the log console
      // for the display.
      if (info != primary) {
        if ((status = log_create_vc(info->graphics, &info->log_vc)) != ZX_OK) {
          break;
        }
      }
      info->bound = true;

      fhd::ImageConfig config = info->image_config;
      if (info->buffer_collection_id) {
        if ((status = import_buffer_collection(info->buffer_collection_id, &config,
                                               &info->image_id)) != ZX_OK) {
          break;
        }
      } else {
        if ((status = import_vmo(info->image_vmo, &config, &info->image_id)) != ZX_OK) {
          break;
        }
      }

      if ((status = set_display_layer(info->id, info->layer_id)) != ZX_OK) {
        break;
      }

      if ((status = configure_layer(info, info->layer_id, info->image_id, &config) != ZX_OK)) {
        break;
      }
    }
  }

  if (status == ZX_OK && apply_configuration() == ZX_OK) {
    // Only listen for logs when we have somewhere to print them. Also,
    // use a repeating wait so that we don't add/remove observers for each
    // log message (which is helpful when tracing the addition/removal of
    // observers).
    set_log_listener_active(true);
    vc_change_graphics(primary->graphics);

    printf("vc: Successfully attached to display %ld\n", primary->id);
    primary_bound = true;
    return ZX_OK;
  } else {
    display_info_t* info = nullptr;
    list_for_every_entry (&display_list, info, display_info_t, node) {
      if (info->image_id) {
        release_image(info->image_id);
        info->image_id = 0;
      }
      if (info->image_vmo) {
        zx_handle_close(info->image_vmo);
        info->image_vmo = 0;
      }
      if (info->graphics) {
        free(info->graphics);
        info->graphics = nullptr;
      }
    }

    if (use_all) {
      return rebind_display(false);
    } else {
      printf("vc: Failed to bind to displays\n");
      return ZX_ERR_INTERNAL;
    }
  }
}

static zx_status_t handle_displays_changed(fidl::VectorView<fhd::Info>& added,
                                           fidl::VectorView<uint64_t>& removed) {
  for (auto& display : added) {
    zx_status_t status = handle_display_added(&display);
    if (status != ZX_OK) {
      return status;
    }
  }

  for (auto& display_id : removed) {
    handle_display_removed(display_id);
  }

  return rebind_display(true);
}

zx_status_t dc_callback_handler(zx_signals_t signals) {
  if (signals & ZX_CHANNEL_PEER_CLOSED) {
    printf("vc: Displays lost\n");
    while (!list_is_empty(&display_list)) {
      handle_display_removed(list_peek_head_type(&display_list, display_info_t, node)->id);
    }
    vc_change_graphics(nullptr);

    zx_handle_close(dc_device);
    dc_client.reset();

    vc_find_display_controller();

    return ZX_ERR_STOP;
  }

  ZX_DEBUG_ASSERT(signals & ZX_CHANNEL_READABLE);
  fhd::Controller::EventHandlers event_handlers{
      .on_displays_changed =
          [](fhd::Controller::OnDisplaysChangedResponse* message) {
            handle_displays_changed(message->added, message->removed);
            return ZX_OK;
          },
      .on_vsync = [](fhd::Controller::OnVsyncResponse* message) { return ZX_OK; },
      .on_client_ownership_change =
          [](fhd::Controller::OnClientOwnershipChangeResponse* message) {
            handle_ownership_change(message->has_ownership);
            return ZX_OK;
          },
      .unknown =
          []() {
            printf("vc: Unknown display callback message\n");
            return ZX_OK;
          }};
  return dc_client->HandleEvents(event_handlers).status();
}

#if BUILD_FOR_DISPLAY_TEST
void initialize_display_channel(zx::channel channel) {
  dc_client = std::make_unique<fhd::Controller::SyncClient>(std::move(channel));

  dc_wait.set_object(dc_client->channel().get());
}

#else

static zx_status_t vc_dc_event(uint32_t evt, const char* name) {
  if ((evt != fuchsia_io_WATCH_EVENT_EXISTING) && (evt != fuchsia_io_WATCH_EVENT_ADDED)) {
    return ZX_OK;
  }

  printf("vc: new display device %s/%s\n", kDisplayControllerDir, name);

  char buf[64];
  snprintf(buf, 64, "%s/%s", kDisplayControllerDir, name);
  fbl::unique_fd fd(open(buf, O_RDWR));
  if (!fd) {
    printf("vc: failed to open display controller device\n");
    return ZX_OK;
  }

  zx::channel device_server, device_client;
  zx_status_t status = zx::channel::create(0, &device_server, &device_client);
  if (status != ZX_OK) {
    return status;
  }

  zx::channel dc_server, dc_client_channel;
  status = zx::channel::create(0, &dc_server, &dc_client_channel);
  if (status != ZX_OK) {
    return status;
  }

  fdio_cpp::FdioCaller caller(std::move(fd));
  auto open_rsp = fhd::Provider::Call::OpenVirtconController(
      caller.channel(), std::move(device_server), std::move(dc_server));
  if (!open_rsp.ok()) {
    return open_rsp.status();
  }
  if (open_rsp->s != ZX_OK) {
    return open_rsp->s;
  }

  dc_device = device_client.release();
  dc_client = std::make_unique<fhd::Controller::SyncClient>(std::move(dc_client_channel));

  zx_handle_close(dc_wait.object());

  status = vc_set_mode(getenv("virtcon.hide-on-boot") == nullptr ? fhd::VirtconMode::FALLBACK
                                                                 : fhd::VirtconMode::INACTIVE);
  if (status != ZX_OK) {
    printf("vc: Failed to set initial ownership %d\n", status);
    vc_find_display_controller();
    return ZX_ERR_STOP;
  }

  ZX_DEBUG_ASSERT(!dc_wait.is_pending());
  dc_wait.set_object(dc_client->channel().get());
  dc_wait.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
  dc_wait.set_handler([](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                         const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
      return;
    }
    status = dc_callback_handler(signal->observed);
    if (status != ZX_OK) {
      return;
    }
    wait->Begin(dispatcher);
  });

  status = dc_wait.Begin(dc_dispatcher);
  if (status != ZX_OK) {
    printf("vc: Failed to set port waiter %d\n", status);
    vc_find_display_controller();
  }
  return ZX_ERR_STOP;
}

#endif  // BUILD_FOR_DISPLAY_TEST

zx_status_t handle_device_dir_event(zx_handle_t handle, zx_signals_t signals,
                                    zx_status_t (*event_handler)(unsigned event, const char* msg)) {
  if (!(signals & ZX_CHANNEL_READABLE)) {
    printf("vc: device directory died\n");
    return ZX_ERR_STOP;
  }

  // Buffer contains events { Opcode, Len, Name[Len] }
  // See zircon/device/vfs.h for more detail
  // extra byte is for temporary NUL
  uint8_t buf[fuchsia_io_MAX_BUF + 1];
  uint32_t len;
  if (zx_channel_read(handle, 0, buf, nullptr, sizeof(buf) - 1, 0, &len, nullptr) < 0) {
    printf("vc: failed to read from device directory\n");
    return ZX_ERR_STOP;
  }

  uint8_t* msg = buf;
  while (len >= 2) {
    uint8_t event = *msg++;
    uint8_t namelen = *msg++;
    if (len < (namelen + 2u)) {
      printf("vc: malformed device directory message\n");
      return ZX_ERR_STOP;
    }
    // add temporary nul
    uint8_t tmp = msg[namelen];
    msg[namelen] = 0;
    zx_status_t status = event_handler(event, reinterpret_cast<char*>(msg));
    if (status != ZX_OK) {
      return status;
    }
    msg[namelen] = tmp;
    msg += namelen;
    len -= (namelen + 2u);
  }
  return ZX_OK;
}

static zx_status_t vc_dc_dir_event_cb(async_dispatcher_t* dispatcher, async::Wait* wait,
                                      zx_status_t status, const zx_packet_signal_t* signal) {
#if BUILD_FOR_DISPLAY_TEST
  return ZX_ERR_NOT_SUPPORTED;
#else
  if (status != ZX_OK) {
    return status;
  }
  status = handle_device_dir_event(wait->object(), signal->observed, vc_dc_event);
  if (status != ZX_OK) {
    return status;
  }
  wait->Begin(dispatcher);
  return ZX_OK;
#endif
}

static void vc_find_display_controller() {
  zx::channel client, server;
  if (zx::channel::create(0, &client, &server) != ZX_OK) {
    printf("vc: Failed to create dc watcher channel\n");
    return;
  }

  fdio_t* fdio = fdio_unsafe_fd_to_io(dc_dir_fd);
  zx_status_t status;
  zx_status_t io_status = fuchsia_io_DirectoryWatch(
      fdio_unsafe_borrow_channel(fdio), fuchsia_io_WATCH_MASK_ALL, 0, server.release(), &status);
  fdio_unsafe_release(fdio);

  if (io_status != ZX_OK || status != ZX_OK) {
    printf("vc: Failed to watch dc directory\n");
    return;
  }

  ZX_DEBUG_ASSERT(!dc_wait.is_pending());

  dc_wait.set_object(client.release());
  dc_wait.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
  dc_wait.set_handler([](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                         const zx_packet_signal_t* signal) {
    vc_dc_dir_event_cb(dispatcher, wait, status, signal);
  });

  status = dc_wait.Begin(dc_dispatcher);
  if (status != ZX_OK) {
    printf("vc: Failed to wait on dc directory\n");
  }
}

bool vc_display_init(async_dispatcher_t* dispatcher) {
  fbl::unique_fd fd(open(kDisplayControllerDir, O_DIRECTORY | O_RDONLY));
  if (!fd) {
    return false;
  }
  dc_dir_fd = fd.release();

  ZX_DEBUG_ASSERT(dc_dispatcher == nullptr);
  dc_dispatcher = dispatcher;
  vc_find_display_controller();

  return true;
}

bool vc_sysmem_connect() {
  zx_status_t status;
  zx::channel sysmem_server, sysmem_client;
  status = zx::channel::create(0, &sysmem_server, &sysmem_client);
  if (status != ZX_OK) {
    return false;
  }
  status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator", sysmem_server.release());
  if (status != ZX_OK) {
    return false;
  }

  sysmem_allocator = std::make_unique<sysmem::Allocator::SyncClient>(std::move(sysmem_client));
  return true;
}
