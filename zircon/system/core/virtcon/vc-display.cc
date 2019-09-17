// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vc-display.h"

#include <fcntl.h>
#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/fidl/coding.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/device/display-controller.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <fbl/unique_fd.h>
#include <port/port.h>

#include "vc.h"

namespace fhd = ::llcpp::fuchsia::hardware::display;

// At any point, |dc_ph| will either be waiting on the display controller device directory
// for a display controller instance or it will be waiting on a display controller interface
// for messages.
static port_handler_t dc_ph;

static std::unique_ptr<fhd::Controller::SyncClient> dc_client;

static struct list_node display_list = LIST_INITIAL_VALUE(display_list);
static bool primary_bound = false;

// remember whether the virtual console controls the display
bool g_vc_owns_display = false;

static void vc_find_display_controller();

#if BUILD_FOR_DISPLAY_TEST

bool is_primary_bound() { return primary_bound; }

struct list_node* get_display_list() {
  return &display_list;
}

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

#if !BUILD_FOR_DISPLAY_TEST
zx_status_t create_layer(uint64_t display_id, uint64_t* layer_id) {
  auto rsp = dc_client->CreateLayer();
  if (!rsp.ok()) {
    printf("vc: Create layer call failed: %d (%s)\n", rsp.status(),
           zx_status_get_string(rsp.status()));
    return rsp.status();
  }
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

#endif  // !BUILD_FOR_DISPLAY_TEST

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
  if (!rsp.ok()) {
    printf("vc: Failed to get single framebuffer: %d (%s)\n", rsp.status(),
           zx_status_get_string(rsp.status()));
    return rsp.status();
  }
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

static zx_status_t allocate_vmo(uint32_t size, zx_handle_t* vmo_out) {
  auto rsp = dc_client->AllocateVmo(size);
  if (!rsp.ok()) {
    printf("vc: Failed to alloc vmo: %d (%s)\n", rsp.status(), zx_status_get_string(rsp.status()));
    return rsp.status();
  }
  if (rsp->res != ZX_OK) {
    printf("vc: Failed to alloc vmo %d\n", rsp->res);
    return rsp->res;
  }
  *vmo_out = rsp->vmo.release();
  return *vmo_out != ZX_HANDLE_INVALID ? ZX_OK : ZX_ERR_INTERNAL;
}

#if !BUILD_FOR_DISPLAY_TEST
zx_status_t import_vmo(zx_handle_t vmo, fhd::ImageConfig* config, uint64_t* id) {
  zx_handle_t vmo_dup;
  zx_status_t status;
  if ((status = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &vmo_dup)) != ZX_OK) {
    printf("vc: Failed to dup fb handle %d\n", status);
    return status;
  }

  auto import_rsp = dc_client->ImportVmoImage(*config, zx::vmo(vmo_dup), 0);
  if (!import_rsp.ok()) {
    zx_status_t status = import_rsp.status();
    printf("vc: Failed to import vmo call %d (%s)\n", status, zx_status_get_string(status));
    return status;
  }

  if (import_rsp->res != ZX_OK) {
    printf("vc: Failed to import vmo %d\n", import_rsp->res);
    return import_rsp->res;
  }

  *id = import_rsp->image_id;
  return ZX_OK;
}

zx_status_t set_display_layer(uint64_t display_id, uint64_t layer_id) {
  auto rsp = dc_client->SetDisplayLayers(display_id,
                                         fidl::VectorView<uint64_t>(&layer_id, layer_id ? 1 : 0));
  if (!rsp.ok()) {
    printf("vc: Failed to set display layers %d\n", rsp.status());
    return rsp.status();
  }

  return ZX_OK;
}

zx_status_t configure_layer(display_info_t* display, uint64_t layer_id, uint64_t image_id,
                            fhd::ImageConfig* config) {
  auto rsp = dc_client->SetLayerPrimaryConfig(layer_id, *config);
  if (!rsp.ok()) {
    printf("vc: Failed to set layer config %d\n", rsp.status());
    return rsp.status();
  }

  auto pos_rsp = dc_client->SetLayerPrimaryPosition(
      layer_id, fhd::Transform::IDENTITY,
      fhd::Frame{.width = config->width, .height = config->height},
      fhd::Frame{.width = display->width, .height = display->height});
  if (!pos_rsp.ok()) {
    printf("vc: Failed to set layer position %d\n", pos_rsp.status());
    return pos_rsp.status();
  }

  auto image_rsp = dc_client->SetLayerImage(layer_id, image_id, 0, 0);
  if (!image_rsp.ok()) {
    printf("vc: Failed to set image %d\n", image_rsp.status());
    return image_rsp.status();
  }
  return ZX_OK;
}

zx_status_t apply_configuration() {
  // Validate and then apply the new configuration
  auto check_rsp = dc_client->CheckConfig(false);
  if (!check_rsp.ok()) {
    printf("vc: Failed to validate display config: %d (%s)\n", check_rsp.status(),
           zx_status_get_string(check_rsp.status()));
    return check_rsp.status();
  }

  if (check_rsp->res != fhd::ConfigResult::OK) {
    printf("vc: Config not valid %d\n", static_cast<int>(check_rsp->res));
    return ZX_ERR_INTERNAL;
  }

  auto rsp = dc_client->ApplyConfig();

  if (!rsp.ok()) {
    printf("vc: Applying config failed %d\n", rsp.status());
    return rsp.status();
  }

  return ZX_OK;
}
#endif  // !BUILD_FOR_DISPLAY_TEST

zx_status_t alloc_display_info_vmo(display_info_t* display) {
  if (get_single_framebuffer(&display->image_vmo, &display->stride) != ZX_OK) {
    auto stride_rsp = dc_client->ComputeLinearImageStride(display->width, display->format);
    if (!stride_rsp.ok()) {
      printf("vc: Failed to compute fb stride: %d (%s)\n", stride_rsp.status(),
             zx_status_get_string(stride_rsp.status()));
      return stride_rsp.status();
    }

    if (stride_rsp->stride < display->width) {
      printf("vc: Got bad stride\n");
      return ZX_ERR_INVALID_ARGS;
    }

    display->stride = stride_rsp->stride;
    uint32_t size = display->stride * display->height * ZX_PIXEL_FORMAT_BYTES(display->format);
    zx_status_t status;
    if ((status = allocate_vmo(size, &display->image_vmo)) != ZX_OK) {
      return ZX_ERR_NO_MEMORY;
    }
  }
  display->image_config.height = display->height;
  display->image_config.width = display->width;
  display->image_config.pixel_format = display->format;
  display->image_config.type = IMAGE_TYPE_SIMPLE;
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

      fhd::ImageConfig config = {.width = info->image_config.width,
                                 .height = info->image_config.height,
                                 .pixel_format = info->image_config.pixel_format,
                                 .type = info->image_config.type};
      if ((status = import_vmo(info->image_vmo, &config, &info->image_id)) != ZX_OK) {
        break;
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

static zx_status_t handle_displays_changed(fidl::VectorView<fhd::Info> added,
                                           fidl::VectorView<uint64_t> removed) {
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

zx_status_t dc_callback_handler(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
  if (signals & ZX_CHANNEL_PEER_CLOSED) {
    printf("vc: Displays lost\n");
    while (!list_is_empty(&display_list)) {
      handle_display_removed(list_peek_head_type(&display_list, display_info_t, node)->id);
    }

    zx_handle_close(dc_device);
    dc_ph.handle = ZX_HANDLE_INVALID;
    dc_client.reset();

    vc_find_display_controller();

    return ZX_ERR_STOP;
  }
  ZX_DEBUG_ASSERT(signals & ZX_CHANNEL_READABLE);

  return dc_client->HandleEvents(fhd::Controller::EventHandlers{
      .displays_changed =
          [](fidl::VectorView<fhd::Info> added, fidl::VectorView<uint64_t> removed) {
            handle_displays_changed(added, removed);
            return ZX_OK;
          },
      .vsync = [](uint64_t display_id, uint64_t timestamp,
                  fidl::VectorView<uint64_t> images) { return ZX_OK; },
      .client_ownership_change =
          [](bool has_ownership) {
            handle_ownership_change(has_ownership);
            return ZX_OK;
          },
      .unknown =
          []() {
            printf("vc: Unknown display callback message\n");
            return ZX_OK;
          }});
}

#if BUILD_FOR_DISPLAY_TEST
void initialize_display_channel(zx::channel channel) {
  dc_client = std::make_unique<fhd::Controller::SyncClient>(std::move(channel));

  dc_ph.handle = dc_client->channel().get();
}
#endif  // BUILD_FOR_DISPLAY_TEST

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

  fzl::FdioCaller caller(std::move(fd));
  auto open_rsp = fhd::Provider::Call::OpenVirtconController(
      caller.channel(), std::move(device_server), std::move(dc_server));
  if (!open_rsp.ok()) {
    return open_rsp.status();
  }
  if (open_rsp->s != ZX_OK) {
    return open_rsp->s;
  }

  dc_device = device_client.release();
  zx_handle_close(dc_ph.handle);
  dc_client = std::make_unique<fhd::Controller::SyncClient>(std::move(dc_client_channel));

  dc_ph.handle = dc_client->channel().get();

  status = vc_set_mode(getenv("virtcon.hide-on-boot") == nullptr ? fhd::VirtconMode::FALLBACK
                                                                 : fhd::VirtconMode::INACTIVE);
  if (status != ZX_OK) {
    printf("vc: Failed to set initial ownership %d\n", status);
    vc_find_display_controller();
    return ZX_ERR_STOP;
  }

  dc_ph.waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
  dc_ph.func = dc_callback_handler;
  if ((status = port_wait(&port, &dc_ph)) != ZX_OK) {
    printf("vc: Failed to set port waiter %d\n", status);
    vc_find_display_controller();
  }
  return ZX_ERR_STOP;
}

static zx_status_t vc_dc_dir_event_cb(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
#if BUILD_FOR_DISPLAY_TEST
  return ZX_ERR_NOT_SUPPORTED;
#else
  return handle_device_dir_event(ph, signals, vc_dc_event);
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

  ZX_DEBUG_ASSERT(dc_ph.handle == ZX_HANDLE_INVALID);
  dc_ph.handle = client.release();
  dc_ph.waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
  dc_ph.func = vc_dc_dir_event_cb;
  if (port_wait(&port, &dc_ph) != ZX_OK) {
    printf("vc: Failed to wait on dc directory\n");
  }
}

bool vc_display_init() {
  fbl::unique_fd fd(open(kDisplayControllerDir, O_DIRECTORY | O_RDONLY));
  if (!fd) {
    return false;
  }
  dc_dir_fd = fd.release();

  vc_find_display_controller();

  return true;
}
