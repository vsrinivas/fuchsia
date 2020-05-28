// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/llcpp/memory.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>

#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>

#include "src/graphics/display/testing/display.h"
#include "src/graphics/display/testing/image.h"

namespace fhd = ::llcpp::fuchsia::hardware::display;

using testing::display::Display;
using testing::display::Image;

static zx_handle_t device_handle;
static std::unique_ptr<fhd::Controller::SyncClient> dc;

static bool has_ownership;

static bool bind_display(const char* controller, fbl::Vector<Display>* displays) {
  fbl::unique_fd fd(open(controller, O_RDWR));
  if (!fd) {
    printf("Failed to open display controller (%d)\n", errno);
    return false;
  }

  zx::channel device_server, device_client;
  auto status = zx::channel::create(0, &device_server, &device_client);
  if (status != ZX_OK) {
    printf("Failed to create device channel %d (%s)\n", status, zx_status_get_string(status));
    return false;
  }

  zx::channel dc_server, dc_client;
  status = zx::channel::create(0, &dc_server, &dc_client);
  if (status != ZX_OK) {
    printf("Failed to create controller channel %d (%s)\n", status, zx_status_get_string(status));
    return false;
  }

  fdio_cpp::FdioCaller caller(std::move(fd));
  auto open_response = fhd::Provider::Call::OpenController(
      caller.channel(), std::move(device_server), std::move(dc_server));
  if (!open_response.ok()) {
    printf("Failed to call service handle %d (%s)\n", open_response.status(),
           open_response.error());
    return false;
  }
  if (open_response->s != ZX_OK) {
    printf("Failed to open controller %d (%s)\n", open_response->s,
           zx_status_get_string(open_response->s));
    return false;
  }

  dc = std::make_unique<fhd::Controller::SyncClient>(std::move(dc_client));
  device_handle = device_client.release();

  // Wait for connection
  std::array<uint8_t, ZX_CHANNEL_MAX_MSG_BYTES> byte_buffer;
  fidl::Message msg(fidl::BytePart(byte_buffer.data(), ZX_CHANNEL_MAX_MSG_BYTES),
                    fidl::HandlePart());
  while (displays->is_empty()) {
    if (ZX_OK != dc->HandleEvents({
                     .on_displays_changed =
                         [&displays](::fidl::VectorView<fhd::Info> added,
                                     ::fidl::VectorView<uint64_t> /*unused*/) {
                           for (size_t i = 0; i < added.count(); i++) {
                             displays->push_back(Display(added[i]));
                           }
                           return ZX_OK;
                         },
                     .on_vsync = [](uint64_t /*unused*/, uint64_t /*unused*/,
                                    ::fidl::VectorView<uint64_t> /*unused*/,
                                    uint64_t /*unused*/) { return ZX_ERR_INVALID_ARGS; },
                     .on_client_ownership_change =
                         [](bool owns) {
                           has_ownership = owns;
                           return ZX_OK;
                         },
                     .unknown = []() { return ZX_ERR_STOP; },
                 })) {
      printf("Got unexpected message\n");
      return false;
    }
  }
  return true;
}

void usage() {
  printf(
      "Usage: display-color red green blue timeout\n\n"
      "red green blue:  Color components between 0-255. (default 255 255 255) \n"
      "timeout: Number of seconds to wait before application exits (default 1 second)"

      "Note: Negative or invalid color values are set to zero, or clamped to 255 if too high\n\n"

      "Note: If timeout is set to 0 and virtual-console is running, the screen may\n"
      "switch back to virtual-console before screen color change is observed by user\n");
}

int main(int argc, const char* argv[]) {
  constexpr uint32_t kDefaultColorWhite = 0xFFFFFFFF;
  constexpr uint32_t kDefaultTimeout = 1;
  constexpr int64_t kLow = 0;
  constexpr int64_t kHigh = 255;
  constexpr uint32_t kAlpha = 0xFF000000;
  constexpr uint32_t kRedShift = 16;
  constexpr uint32_t kRedMask = 0x00ff0000;
  constexpr uint32_t kGreenShift = 8;
  constexpr uint32_t kGreenMask = 0x0000ff00;
  constexpr uint32_t kBlueMask = 0x000000ff;

  uint32_t color = kDefaultColorWhite;
  uint64_t timeout = kDefaultTimeout;
  if (argc >= 4 && argc <= 5) {
    auto red = strtol(argv[1], nullptr, 10);
    auto green = strtol(argv[2], nullptr, 10);
    auto blue = strtol(argv[3], nullptr, 10);
    red = std::clamp(red, kLow, kHigh);
    green = std::clamp(green, kLow, kHigh);
    blue = std::clamp(blue, kLow, kHigh);
    color = (kAlpha) | ((red << kRedShift) & kRedMask) | ((green << kGreenShift) & kGreenMask) |
            (blue & kBlueMask);

    if (argc == 5) {
      timeout = strtoul(argv[4], nullptr, 10);
    }
  } else if (argc != 1) {
    printf("Invalid Argument\n");
    usage();
    return -1;
  }

  fbl::Vector<Display> displays;
  fbl::Vector<uint64_t> display_layers;
  const char* controller = "/dev/class/display-controller/000";
  // bind to display first
  if (!bind_display(controller, &displays)) {
    return -1;
  }

  // make sure we have display connected
  if (displays.is_empty()) {
    printf("No displays available\n");
    return 0;
  }

  // create a later
  auto layer_id = dc->CreateLayer();
  display_layers.push_back(layer_id->layer_id);
  dc->SetDisplayLayers(displays[0].id(),
                       {fidl::unowned_ptr(display_layers.data()), display_layers.size()});
  // create an image
  auto* image = Image::Create(dc.get(), displays[0].mode().horizontal_resolution,
                              displays[0].mode().vertical_resolution, displays[0].format(), color,
                              color, false);

  // import image
  testing::display::image_import_t import_out;
  image->Import(dc.get(), &import_out);
  fhd::ImageConfig image_config;
  image->GetConfig(&image_config);
  dc->SetLayerPrimaryConfig(layer_id->layer_id, image_config);

  // set image to layer
  dc->SetLayerImage(layer_id->layer_id, import_out.id, 0, 0);

  image->Render(-1, -1);

  dc->ApplyConfig();

  if (timeout) {
    zx_nanosleep(zx_deadline_after(ZX_SEC(timeout)));
  }

  return 0;
}
