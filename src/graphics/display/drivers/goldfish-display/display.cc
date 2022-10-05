// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "display.h"

#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#include <fidl/fuchsia.sysmem/cpp/fidl.h>
#include <fuchsia/hardware/goldfish/control/cpp/banjo.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/cpp/wait.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/trace/event.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/fit/defer.h>
#include <lib/zircon-internal/align.h>
#include <zircon/pixelformat.h>
#include <zircon/status.h>
#include <zircon/threads.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <sstream>
#include <vector>

#include <fbl/auto_lock.h>

#include "fidl/fuchsia.sysmem/cpp/common_types.h"
#include "fidl/fuchsia.sysmem/cpp/markers.h"
#include "fidl/fuchsia.sysmem/cpp/natural_types.h"
#include "lib/fidl/cpp/channel.h"
#include "lib/fidl/cpp/wire/internal/transport_channel.h"
#include "lib/fidl/cpp/wire/traits.h"
#include "src/devices/lib/goldfish/pipe_headers/include/base.h"
#include "src/graphics/display/drivers/goldfish-display/goldfish-display-bind.h"
#include "src/graphics/display/drivers/goldfish-display/render_control.h"

namespace goldfish {
namespace {

const char* kTag = "goldfish-display";

constexpr uint64_t kPrimaryDisplayId = 1;

constexpr zx_pixel_format_t kPixelFormats[] = {
    ZX_PIXEL_FORMAT_RGB_x888,
    ZX_PIXEL_FORMAT_ARGB_8888,
    ZX_PIXEL_FORMAT_BGR_888x,
    ZX_PIXEL_FORMAT_ABGR_8888,
};

constexpr uint32_t FB_WIDTH = 1;
constexpr uint32_t FB_HEIGHT = 2;
constexpr uint32_t FB_FPS = 5;

constexpr uint32_t GL_RGBA = 0x1908;
constexpr uint32_t GL_BGRA_EXT = 0x80E1;

}  // namespace

// static
zx_status_t Display::Create(void* ctx, zx_device_t* device) {
  auto display = std::make_unique<Display>(device);

  zx_status_t status = display->Bind();
  if (status == ZX_OK) {
    // devmgr now owns device.
    __UNUSED auto* dev = display.release();
  }
  return status;
}

Display::Display(zx_device_t* parent)
    : DisplayType(parent), loop_(&kAsyncLoopConfigNeverAttachToThread) {
  if (parent) {
    control_ = parent;
  }
}

Display::~Display() {
  loop_.Shutdown();

  for (auto& it : devices_) {
    TeardownDisplay(it.first);
  }
}

zx_status_t Display::Bind() {
  fbl::AutoLock lock(&lock_);

  if (!control_.is_valid()) {
    zxlogf(ERROR, "%s: no control protocol", kTag);
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_goldfish_pipe::GoldfishPipe>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  auto channel = endpoints->server.TakeChannel();

  zx_status_t status = control_.ConnectToPipeDevice(std::move(channel));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not connect to pipe device: %s", kTag, zx_status_get_string(status));
    return status;
  }

  pipe_ = fidl::WireSyncClient(std::move(endpoints->client));
  if (!pipe_.is_valid()) {
    zxlogf(ERROR, "%s: no pipe protocol", kTag);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Create a second FIDL connection for use by RenderControl.
  endpoints = fidl::CreateEndpoints<fuchsia_hardware_goldfish_pipe::GoldfishPipe>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  channel = endpoints->server.TakeChannel();

  status = control_.ConnectToPipeDevice(std::move(channel));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not connect to pipe device: %s", kTag, zx_status_get_string(status));
    return status;
  }

  fidl::WireSyncClient pipe_client{std::move(endpoints->client)};

  rc_ = std::make_unique<RenderControl>();
  status = rc_->InitRcPipe(std::move(pipe_client));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: RenderControl failed to initialize: %d", kTag, status);
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint64_t next_display_id = kPrimaryDisplayId;

  // Parse optional display params. This is comma seperated list of
  // display devices. The format is:
  //
  // widthxheight[-xpos+ypos][@refresh][%scale]
  char* flag = getenv("driver.goldfish.displays");
  if (flag) {
    std::stringstream devices_stream(flag);
    std::string device_string;
    while (std::getline(devices_stream, device_string, ',')) {
      Device device;
      char delim = 0;
      std::stringstream device_stream(device_string);
      do {
        switch (delim) {
          case 0:
            device_stream >> device.width;
            break;
          case 'x':
            device_stream >> device.height;
            break;
          case '-':
            device_stream >> device.x;
            break;
          case '+':
            device_stream >> device.y;
            break;
          case '@':
            device_stream >> device.refresh_rate_hz;
            break;
          case '%':
            device_stream >> device.scale;
            break;
        }
      } while (device_stream >> delim);

      if (!device.width || !device.height) {
        zxlogf(ERROR, "%s: skip device=%s, missing size", kTag, device_string.c_str());
        continue;
      }
      if (!device.refresh_rate_hz) {
        zxlogf(ERROR, "%s: skip device=%s, refresh rate is zero", kTag, device_string.c_str());
        continue;
      }
      if (device.scale < 0.1f || device.scale > 100.f) {
        zxlogf(ERROR, "%s: skip device=%s, scale is not in range 0.1-100", kTag,
               device_string.c_str());
        continue;
      }

      auto& new_device = devices_[next_display_id++];
      new_device.width = device.width;
      new_device.height = device.height;
      new_device.x = device.x;
      new_device.y = device.y;
      new_device.refresh_rate_hz = device.refresh_rate_hz;
      new_device.scale = device.scale;
    }
  }

  // Create primary device if needed.
  if (devices_.empty()) {
    auto& device = devices_[kPrimaryDisplayId];
    device.width = static_cast<uint32_t>(rc_->GetFbParam(FB_WIDTH, 1024));
    device.height = static_cast<uint32_t>(rc_->GetFbParam(FB_HEIGHT, 768));
    device.refresh_rate_hz = static_cast<uint32_t>(rc_->GetFbParam(FB_FPS, 60));
  }

  // Set up display and set up flush task for each device.
  for (auto& it : devices_) {
    zx_status_t status = SetupDisplay(it.first);
    ZX_DEBUG_ASSERT(status == ZX_OK);

    async::PostTask(loop_.dispatcher(), [this, display_id = it.first] {
      FlushDisplay(loop_.dispatcher(), display_id);
    });
  }

  // Start async event thread.
  loop_.StartThread("goldfish_display_event_thread");

  return DdkAdd("goldfish-display");
}

void Display::DdkRelease() { delete this; }

void Display::DisplayControllerImplSetDisplayControllerInterface(
    const display_controller_interface_protocol_t* interface) {
  std::vector<added_display_args_t> args;
  for (auto& it : devices_) {
    added_display_args_t display = {
        .display_id = it.first,
        .edid_present = false,
        .panel =
            {
                .params =
                    {
                        .width = it.second.width,
                        .height = it.second.height,
                        .refresh_rate_e2 = it.second.refresh_rate_hz * 100,
                    },
            },
        .pixel_format_list = kPixelFormats,
        .pixel_format_count = sizeof(kPixelFormats) / sizeof(kPixelFormats[0]),
        .cursor_info_list = nullptr,
        .cursor_info_count = 0,
    };
    args.push_back(display);
  }

  {
    fbl::AutoLock lock(&flush_lock_);
    dc_intf_ = ddk::DisplayControllerInterfaceProtocolClient(interface);
    dc_intf_.OnDisplaysChanged(args.data(), args.size(), nullptr, 0, nullptr, 0, nullptr);
  }
}

zx_status_t Display::ImportVmoImage(image_t* image, zx::vmo vmo, size_t offset) {
  auto color_buffer = std::make_unique<ColorBuffer>();

  // Linear images must be pinned.
  unsigned pixel_size = ZX_PIXEL_FORMAT_BYTES(image->pixel_format);
  color_buffer->size = ZX_ROUNDUP(image->width * image->height * pixel_size, PAGE_SIZE);
  color_buffer->pinned_vmo =
      rc_->pipe_io()->PinVmo(vmo, ZX_BTI_PERM_READ | ZX_BTI_CONTIGUOUS, offset, color_buffer->size);

  uint32_t format = (image->pixel_format == ZX_PIXEL_FORMAT_RGB_x888 ||
                     image->pixel_format == ZX_PIXEL_FORMAT_ARGB_8888)
                        ? GL_BGRA_EXT
                        : GL_RGBA;

  color_buffer->vmo = std::move(vmo);
  color_buffer->width = image->width;
  color_buffer->height = image->height;
  color_buffer->format = format;

  auto status = rc_->CreateColorBuffer(image->width, image->height, format);
  if (status.is_error()) {
    zxlogf(ERROR, "%s: failed to create color buffer", kTag);
    return status.error_value();
  }
  color_buffer->id = status.value();

  image->handle = reinterpret_cast<uint64_t>(color_buffer.release());
  return ZX_OK;
}

zx_status_t Display::DisplayControllerImplImportImage(image_t* image, zx_unowned_handle_t handle,
                                                      uint32_t index) {
  auto wait_result = fidl::Call(fidl::UnownedClientEnd<fuchsia_sysmem::BufferCollection>(handle))
                         ->WaitForBuffersAllocated();
  if (wait_result.is_error()) {
    return wait_result.error_value().status();
  }
  if (wait_result->status() != ZX_OK) {
    return wait_result->status();
  }
  auto& collection_info = wait_result->buffer_collection_info();

  zx::vmo vmo;
  if (index < collection_info.buffer_count()) {
    vmo = std::move(collection_info.buffers()[index].vmo());
    ZX_DEBUG_ASSERT(!collection_info.buffers()[index].vmo().is_valid());
  }

  if (!vmo.is_valid()) {
    zxlogf(ERROR, "%s: invalid index", kTag);
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint64_t offset = collection_info.buffers()[index].vmo_usable_start();

  if (collection_info.settings().buffer_settings().heap() !=
      fuchsia_sysmem::HeapType::kGoldfishDeviceLocal) {
    return ImportVmoImage(image, std::move(vmo), offset);
  }

  if (!collection_info.settings().has_image_format_constraints() || offset) {
    zxlogf(ERROR, "%s: invalid image format or offset", kTag);
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto color_buffer = std::make_unique<ColorBuffer>();
  color_buffer->vmo = std::move(vmo);
  image->handle = reinterpret_cast<uint64_t>(color_buffer.release());
  return ZX_OK;
}

void Display::DisplayControllerImplReleaseImage(image_t* image) {
  auto color_buffer = reinterpret_cast<ColorBuffer*>(image->handle);

  // Color buffer is owned by image in the linear case.
  if (image->type == IMAGE_TYPE_SIMPLE) {
    rc_->CloseColorBuffer(color_buffer->id);
  }

  async::PostTask(loop_.dispatcher(), [this, color_buffer] {
    for (auto& kv : devices_) {
      if (kv.second.incoming_config.has_value() &&
          kv.second.incoming_config->color_buffer == color_buffer) {
        kv.second.incoming_config = std::nullopt;
      }
    }
    delete color_buffer;
  });
}

uint32_t Display::DisplayControllerImplCheckConfiguration(const display_config_t** display_configs,
                                                          size_t display_count,
                                                          uint32_t** layer_cfg_results,
                                                          size_t* layer_cfg_result_count) {
  if (display_count == 0) {
    return CONFIG_DISPLAY_OK;
  }
  for (unsigned i = 0; i < display_count; i++) {
    const size_t layer_count = display_configs[i]->layer_count;
    if (layer_count > 0) {
      ZX_DEBUG_ASSERT(devices_.find(display_configs[i]->display_id) != devices_.end());
      const Device& device = devices_[display_configs[i]->display_id];

      if (display_configs[i]->cc_flags != 0) {
        // Color Correction is not supported, but we will pretend we do.
        // TODO(fxbug.dev/36184): Returning error will cause blank screen if scenic requests
        // color correction. For now, lets pretend we support it, until a proper
        // fix is done (either from scenic or from core display)
        zxlogf(WARNING, "%s: Color Correction not support. No error reported", __func__);
      }

      if (display_configs[i]->layer_list[0]->type != LAYER_TYPE_PRIMARY) {
        // We only support PRIMARY layer. Notify client to convert layer to
        // primary type.
        layer_cfg_results[i][0] |= CLIENT_USE_PRIMARY;
        layer_cfg_result_count[i] = 1;
      } else {
        primary_layer_t* layer = &display_configs[i]->layer_list[0]->cfg.primary;
        // Scaling is allowed if destination frame match display and
        // source frame match image.
        frame_t dest_frame = {
            .x_pos = 0,
            .y_pos = 0,
            .width = device.width,
            .height = device.height,
        };
        frame_t src_frame = {
            .x_pos = 0,
            .y_pos = 0,
            .width = layer->image.width,
            .height = layer->image.height,
        };
        if (memcmp(&layer->dest_frame, &dest_frame, sizeof(frame_t)) != 0) {
          // TODO(fxbug.dev/36222): Need to provide proper flag to indicate driver only
          // accepts full screen dest frame.
          layer_cfg_results[i][0] |= CLIENT_FRAME_SCALE;
        }
        if (memcmp(&layer->src_frame, &src_frame, sizeof(frame_t)) != 0) {
          layer_cfg_results[i][0] |= CLIENT_SRC_FRAME;
        }

        if (layer->alpha_mode != ALPHA_DISABLE) {
          // Alpha is not supported.
          layer_cfg_results[i][0] |= CLIENT_ALPHA;
        }

        if (layer->transform_mode != FRAME_TRANSFORM_IDENTITY) {
          // Transformation is not supported.
          layer_cfg_results[i][0] |= CLIENT_TRANSFORM;
        }

        // Check if any changes to the base layer were required.
        if (layer_cfg_results[i][0] != 0) {
          layer_cfg_result_count[i] = 1;
        }
      }
      // If there is more than one layer, the rest need to be merged into the base layer.
      if (layer_count > 1) {
        layer_cfg_results[i][0] |= CLIENT_MERGE_BASE;
        for (unsigned j = 1; j < layer_count; j++) {
          layer_cfg_results[i][j] |= CLIENT_MERGE_SRC;
        }
        layer_cfg_result_count[i] = layer_count;
      }
    }
  }
  return CONFIG_DISPLAY_OK;
}

zx_status_t Display::PresentDisplayConfig(RenderControl::DisplayId display_id,
                                          const DisplayConfig& display_config) {
  auto* color_buffer = display_config.color_buffer;
  if (!color_buffer) {
    return ZX_OK;
  }

  zx::eventpair event_display, event_sync_device;
  zx_status_t status = zx::eventpair::create(0u, &event_display, &event_sync_device);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: zx_eventpair_create failed: %d", kTag, status);
    return status;
  }

  auto& device = devices_[display_id];

  // Set up async wait for the goldfish sync event. The zx::eventpair will be
  // stored in the async wait callback, which will be destroyed only when the
  // event is signaled or the wait is cancelled.
  device.pending_config_waits.emplace_back(event_display.get(), ZX_EVENTPAIR_SIGNALED, 0);
  auto& wait = device.pending_config_waits.back();

  wait.Begin(loop_.dispatcher(), [event = std::move(event_display), &device,
                                  pending_config_stamp = display_config.config_stamp](
                                     async_dispatcher_t* dispatcher, async::WaitOnce* current_wait,
                                     zx_status_t status, const zx_packet_signal_t*) {
    TRACE_DURATION("gfx", "Display::SyncEventHandler", "config_stamp", pending_config_stamp.value);
    if (status == ZX_ERR_CANCELED) {
      zxlogf(INFO, "Wait for config stamp %lu cancelled.", pending_config_stamp.value);
      return;
    }
    ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "Invalid wait status: %d\n", status);

    // When the eventpair in |current_wait| is signalled, all the pending waits
    // that are queued earlier than that eventpair will be removed from the list
    // and the async WaitOnce will be cancelled.
    // Note that the cancelled waits will return early and will not reach here.
    ZX_DEBUG_ASSERT(std::any_of(device.pending_config_waits.begin(),
                                device.pending_config_waits.end(),
                                [current_wait](const async::WaitOnce& wait) {
                                  return wait.object() == current_wait->object();
                                }));
    // Remove all the pending waits that are queued earlier than the current
    // wait, and the current wait itself. In WaitOnce, the callback is moved to
    // stack before current wait is removed, so it's safe to remove any item in
    // the list.
    for (auto it = device.pending_config_waits.begin(); it != device.pending_config_waits.end();) {
      if (it->object() == current_wait->object()) {
        device.pending_config_waits.erase(it);
        break;
      }
      it = device.pending_config_waits.erase(it);
    }
    device.latest_config_stamp = {
        .value = std::max(device.latest_config_stamp.value, pending_config_stamp.value)};
  });

  // Update host-writeable display buffers before presenting.
  if (color_buffer->pinned_vmo.region_count() > 0) {
    auto status =
        rc_->UpdateColorBuffer(color_buffer->id, color_buffer->pinned_vmo, color_buffer->width,
                               color_buffer->height, color_buffer->format, color_buffer->size);
    if (status.is_error() || status.value()) {
      zxlogf(ERROR, "%s : color buffer update failed: %d:%u", kTag, status.status_value(),
             status.value_or(0));
      return status.is_error() ? status.status_value() : ZX_ERR_INTERNAL;
    }
  }

  // Present the buffer.
  {
    uint32_t host_display_id = devices_[display_id].host_display_id;
    if (host_display_id) {
      // Set color buffer for secondary displays.
      auto status = rc_->SetDisplayColorBuffer(host_display_id, color_buffer->id);
      if (status.is_error() || status.value()) {
        zxlogf(ERROR, "%s: failed to set display color buffer", kTag);
        return status.is_error() ? status.status_value() : ZX_ERR_INTERNAL;
      }
    } else {
      status = rc_->FbPost(color_buffer->id);
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s: FbPost failed: %d", kTag, status);
        return status;
      }
    }

    fbl::AutoLock lock(&lock_);
    status = control_.CreateSyncFence(std::move(event_sync_device));
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: CreateSyncFence failed: %d", kTag, status);
      return status;
    }
  }

  return ZX_OK;
}

void Display::DisplayControllerImplApplyConfiguration(const display_config_t** display_configs,
                                                      size_t display_count,
                                                      const config_stamp_t* config_stamp) {
  for (const auto& it : devices_) {
    uint64_t handle = 0;
    for (unsigned i = 0; i < display_count; i++) {
      if (display_configs[i]->display_id == it.first) {
        if (display_configs[i]->layer_count) {
          handle = display_configs[i]->layer_list[0]->cfg.primary.image.handle;
        }
        break;
      }
    }

    if (handle == 0u) {
      // The display doesn't have any active layers right now. For layers that
      // previously existed, we should cancel waiting events on the pending
      // color buffer and remove references to both pending and current color
      // buffers.
      async::PostTask(
          loop_.dispatcher(), [this, display_id = it.first, config_stamp = *config_stamp] {
            if (devices_.find(display_id) != devices_.end()) {
              auto& device = devices_[display_id];
              device.pending_config_waits.clear();
              device.incoming_config = std::nullopt;
              device.latest_config_stamp = {
                  .value = std::max(device.latest_config_stamp.value, config_stamp.value)};
            }
          });
      return;
    }

    auto color_buffer = reinterpret_cast<ColorBuffer*>(handle);
    if (color_buffer && !color_buffer->id) {
      zx::vmo vmo;

      zx_status_t status = color_buffer->vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to duplicate vmo: %d", kTag, status);
      } else {
        fbl::AutoLock lock(&lock_);
        status = control_.GetColorBuffer(std::move(vmo), &color_buffer->id);
        if (status != ZX_OK) {
          zxlogf(ERROR, "%s: failed to get color buffer: %d", kTag, status);
        }

        // Color buffers are in vulkan-only mode by default as that avoids
        // unnecessary copies on the host in some cases. The color buffer
        // needs to be moved out of vulkan-only mode before being used for
        // presentation.
        if (color_buffer->id) {
          auto status = rc_->SetColorBufferVulkanMode(color_buffer->id, 0);
          if (status.is_error() || status.value()) {
            zxlogf(ERROR, "%s: failed to set vulkan mode: %d %d", kTag, status.status_value(),
                   status.value_or(0));
          }
        }
      }
    }

    if (color_buffer) {
      async::PostTask(loop_.dispatcher(),
                      [this, config_stamp = *config_stamp, color_buffer, display_id = it.first] {
                        devices_[display_id].incoming_config = {
                            .color_buffer = color_buffer,
                            .config_stamp = config_stamp,
                        };
                      });
    }
  }
}

zx_status_t Display::DisplayControllerImplGetSysmemConnection(zx::channel connection) {
  fbl::AutoLock lock(&lock_);
  auto result = pipe_->ConnectSysmem(std::move(connection));
  zx_status_t status = result.status();
  if (!result.ok()) {
    zxlogf(ERROR, "%s: failed to connect to sysmem: %s", kTag, result.status_string());
  }
  return status;
}

zx_status_t Display::DisplayControllerImplSetBufferCollectionConstraints(const image_t* config,
                                                                         uint32_t collection) {
  fuchsia_sysmem::BufferCollectionConstraints constraints;
  constraints.usage().display() = fuchsia_sysmem::kDisplayUsageLayer;
  constraints.has_buffer_memory_constraints() = true;
  auto& buffer_constraints = constraints.buffer_memory_constraints();
  buffer_constraints.min_size_bytes() = 0;
  buffer_constraints.max_size_bytes() = 0xffffffff;
  buffer_constraints.physically_contiguous_required() = true;
  buffer_constraints.secure_required() = false;
  buffer_constraints.ram_domain_supported() = true;
  buffer_constraints.cpu_domain_supported() = true;
  buffer_constraints.inaccessible_domain_supported() = true;
  buffer_constraints.heap_permitted_count() = 2;
  buffer_constraints.heap_permitted()[0] = fuchsia_sysmem::HeapType::kSystemRam;
  buffer_constraints.heap_permitted()[1] = fuchsia_sysmem::HeapType::kGoldfishDeviceLocal;
  constraints.image_format_constraints_count() = 4;
  for (uint32_t i = 0; i < constraints.image_format_constraints_count(); i++) {
    auto& image_constraints = constraints.image_format_constraints()[i];
    image_constraints.pixel_format().type() = i & 0b01 ? fuchsia_sysmem::PixelFormatType::kR8G8B8A8
                                                       : fuchsia_sysmem::PixelFormatType::kBgra32;
    image_constraints.pixel_format().has_format_modifier() = true;
    image_constraints.pixel_format().format_modifier().value() =
        i & 0b10 ? fuchsia_sysmem::kFormatModifierLinear
                 : fuchsia_sysmem::kFormatModifierGoogleGoldfishOptimal;
    image_constraints.color_spaces_count() = 1;
    image_constraints.color_space()[0].type() = fuchsia_sysmem::ColorSpaceType::kSrgb;
    image_constraints.min_coded_width() = 0;
    image_constraints.max_coded_width() = 0xffffffff;
    image_constraints.min_coded_height() = 0;
    image_constraints.max_coded_height() = 0xffffffff;
    image_constraints.min_bytes_per_row() = 0;
    image_constraints.max_bytes_per_row() = 0xffffffff;
    image_constraints.max_coded_width_times_coded_height() = 0xffffffff;
    image_constraints.layers() = 1;
    image_constraints.coded_width_divisor() = 1;
    image_constraints.coded_height_divisor() = 1;
    image_constraints.bytes_per_row_divisor() = 1;
    image_constraints.start_offset_divisor() = 1;
    image_constraints.display_width_divisor() = 1;
    image_constraints.display_height_divisor() = 1;
  }

  auto set_result = fidl::Call(fidl::UnownedClientEnd<fuchsia_sysmem::BufferCollection>(collection))
                        ->SetConstraints({true, std::move(constraints)});
  if (set_result.is_error()) {
    zxlogf(ERROR, "%s: failed to set constraints", kTag);
    return set_result.error_value().status();
  }

  return ZX_OK;
}

zx_status_t Display::SetupDisplay(uint64_t display_id) {
  Device& device = devices_[display_id];

  // Create secondary displays.
  if (display_id != kPrimaryDisplayId) {
    auto status = rc_->CreateDisplay();
    if (status.is_error()) {
      return status.error_value();
    }
    device.host_display_id = status.value();
  }
  uint32_t width = static_cast<uint32_t>(static_cast<float>(device.width) * device.scale);
  uint32_t height = static_cast<uint32_t>(static_cast<float>(device.height) * device.scale);
  auto status = rc_->SetDisplayPose(device.host_display_id, device.x, device.y, width, height);
  if (status.is_error() || status.value()) {
    zxlogf(ERROR, "%s: failed to set display pose: %d %d", kTag, status.status_value(),
           status.value_or(0));
    return status.is_error() ? status.error_value() : ZX_ERR_INTERNAL;
  }
  device.expected_next_flush = async::Now(loop_.dispatcher());

  return ZX_OK;
}

void Display::TeardownDisplay(uint64_t display_id) {
  Device& device = devices_[display_id];

  if (device.host_display_id) {
    zx::status<uint32_t> status = rc_->DestroyDisplay(device.host_display_id);
    ZX_DEBUG_ASSERT(status.is_ok());
    ZX_DEBUG_ASSERT(!status.value());
  }
}

void Display::FlushDisplay(async_dispatcher_t* dispatcher, uint64_t display_id) {
  Device& device = devices_[display_id];

  zx::duration period = zx::sec(1) / device.refresh_rate_hz;
  zx::time expected_next_flush = device.expected_next_flush + period;

  if (device.incoming_config.has_value()) {
    zx_status_t status = PresentDisplayConfig(display_id, *device.incoming_config);
    ZX_DEBUG_ASSERT(status == ZX_OK || status == ZX_ERR_SHOULD_WAIT);
  }

  {
    fbl::AutoLock lock(&flush_lock_);

    if (dc_intf_.is_valid()) {
      zx::time now = async::Now(dispatcher);
      dc_intf_.OnDisplayVsync(display_id, now.get(), &device.latest_config_stamp);
    }
  }

  // If we've already passed the |expected_next_flush| deadline, skip the
  // Vsync and adjust the deadline to the earliest next available frame.
  zx::time now = async::Now(dispatcher);
  if (now > expected_next_flush) {
    expected_next_flush +=
        period * (((now - expected_next_flush + period).get() - 1L) / period.get());
  }

  device.expected_next_flush = expected_next_flush;
  async::PostTaskForTime(
      dispatcher, [this, dispatcher, display_id] { FlushDisplay(dispatcher, display_id); },
      expected_next_flush);
}

}  // namespace goldfish

static constexpr zx_driver_ops_t goldfish_display_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = goldfish::Display::Create;
  return ops;
}();

ZIRCON_DRIVER(goldfish_display, goldfish_display_driver_ops, "zircon", "0.1");
