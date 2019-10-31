// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fzl/fdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/pixelformat.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <memory>

//#include <ddk/protocol/display/controller.h>
#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>

#include "display.h"
#include "virtual-layer.h"

namespace fhd = ::llcpp::fuchsia::hardware::display;

static zx_handle_t device_handle;
static std::unique_ptr<fhd::Controller::SyncClient> dc;
static bool has_ownership;

static bool wait_for_driver_event(zx_time_t deadline) {
  zx_handle_t observed;
  uint32_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
  if (zx_object_wait_one(dc->channel().get(), signals, ZX_TIME_INFINITE, &observed) != ZX_OK) {
    printf("Wait failed\n");
    return false;
  }
  if (observed & ZX_CHANNEL_PEER_CLOSED) {
    printf("Display controller died\n");
    return false;
  }
  return true;
}

static bool bind_display(fbl::Vector<Display>* displays) {
  printf("Opening controller\n");
  fbl::unique_fd fd(open("/dev/class/display-controller/000", O_RDWR));
  if (!fd) {
    printf("Failed to open display controller (%d)\n", errno);
    return false;
  }

  zx::channel device_server, device_client;
  zx_status_t status = zx::channel::create(0, &device_server, &device_client);
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

  fzl::FdioCaller caller(std::move(fd));
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

  uint8_t byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
  fidl::Message msg(fidl::BytePart(byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES), fidl::HandlePart());
  while (displays->is_empty()) {
    printf("Waiting for display\n");
    if (ZX_OK !=
        dc->HandleEvents({
            .displays_changed =
                [&displays](::fidl::VectorView<fhd::Info> added,
                            ::fidl::VectorView<uint64_t> removed) {
                  for (size_t i = 0; i < added.count(); i++) {
                    displays->push_back(Display(added[i]));
                  }
                  return ZX_OK;
                },
            .vsync = [](uint64_t display_id, uint64_t timestamp,
                        ::fidl::VectorView<uint64_t> images) { return ZX_ERR_INVALID_ARGS; },
            .client_ownership_change =
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

  if (!dc->EnableVsync(true).ok()) {
    printf("Failed to enable vsync\n");
    return false;
  }

  return true;
}

Display* find_display(fbl::Vector<Display>& displays, const char* id_str) {
  uint64_t id = strtoul(id_str, nullptr, 10);
  if (id != 0) {  // 0 is the invalid id, and luckily what strtoul returns on failure
    for (auto& d : displays) {
      if (d.id() == id) {
        return &d;
      }
    }
  }
  return nullptr;
}

bool update_display_layers(const fbl::Vector<std::unique_ptr<VirtualLayer>>& layers,
                           const Display& display, fbl::Vector<uint64_t>* current_layers) {
  fbl::Vector<uint64_t> new_layers;

  for (auto& layer : layers) {
    uint64_t id = layer->id(display.id());
    if (id != fhd::invalidId) {
      new_layers.push_back(id);
    }
  }

  bool layer_change = new_layers.size() != current_layers->size();
  if (!layer_change) {
    for (unsigned i = 0; i < new_layers.size(); i++) {
      if (new_layers[i] != (*current_layers)[i]) {
        layer_change = true;
        break;
      }
    }
  }

  if (layer_change) {
    current_layers->swap(new_layers);
    if (!dc->SetDisplayLayers(display.id(), {current_layers->data(), current_layers->size()})
             .ok()) {
      printf("Failed to set layers\n");
      return false;
    }
  }
  return true;
}

bool apply_config() {
  auto result = dc->CheckConfig(false);
  if (!result.ok()) {
    printf("Failed to make check call: %d (%s)\n", result.status(), result.error());
    return false;
  }

  if (result->res != fhd::ConfigResult::OK) {
    printf("Config not valid (%d)\n", static_cast<uint32_t>(result->res));
    for (const auto& op : result->ops) {
      printf("Client composition op (display %ld, layer %ld): %hhu\n", op.display_id, op.layer_id,
             static_cast<uint8_t>(op.opcode));
    }
    return false;
  }

  if (!dc->ApplyConfig().ok()) {
    printf("Apply failed\n");
    return false;
  }
  return true;
}

zx_status_t wait_for_vsync(const fbl::Vector<std::unique_ptr<VirtualLayer>>& layers) {
  fhd::Controller::EventHandlers handlers = {
      .displays_changed =
          [](::fidl::VectorView<fhd::Info>, ::fidl::VectorView<uint64_t>) {
            printf("Display disconnected\n");
            return ZX_ERR_STOP;
          },
      .vsync =
          [&layers](uint64_t display_id, uint64_t timestamp, ::fidl::VectorView<uint64_t> images) {
            for (auto& layer : layers) {
              uint64_t id = layer->image_id(display_id);
              if (id == 0) {
                continue;
              }
              for (auto image_id : images) {
                if (image_id == id) {
                  layer->set_frame_done(display_id);
                }
              }
            }

            for (auto& layer : layers) {
              if (!layer->is_done()) {
                return ZX_ERR_NEXT;
              }
            }
            return ZX_OK;
          },
      .client_ownership_change =
          [](bool owned) {
            has_ownership = owned;
            return ZX_ERR_NEXT;
          },
      .unknown = []() { return ZX_ERR_STOP; },
  };
  return dc->HandleEvents(std::move(handlers));
}

int main(int argc, const char* argv[]) {
  printf("Running display test\n");

  fbl::Vector<Display> displays;
  fbl::Vector<fbl::Vector<uint64_t>> display_layers;
  fbl::Vector<std::unique_ptr<VirtualLayer>> layers;
  int32_t num_frames = 120;  // default to 120 frames
  int32_t delay = 0;
  enum Platform {
    SIMPLE,
    INTEL,
    ARM_MEDIATEK,
    ARM_AMLOGIC,
  };
  Platform platform = INTEL;  // default to Intel

  if (!bind_display(&displays)) {
    return -1;
  }

  if (displays.is_empty()) {
    printf("No displays available\n");
    return 0;
  }

  for (unsigned i = 0; i < displays.size(); i++) {
    display_layers.push_back(fbl::Vector<uint64_t>());
  }

  argc--;
  argv++;

  while (argc) {
    if (strcmp(argv[0], "--dump") == 0) {
      for (auto& display : displays) {
        display.Dump();
      }
      return 0;
    } else if (strcmp(argv[0], "--mode-set") == 0 || strcmp(argv[0], "--format-set") == 0) {
      Display* display = find_display(displays, argv[1]);
      if (!display) {
        printf("Invalid display \"%s\" for %s\n", argv[1], argv[0]);
        return -1;
      }
      if (strcmp(argv[0], "--mode-set") == 0) {
        if (!display->set_mode_idx(atoi(argv[2]))) {
          printf("Invalid mode id\n");
          return -1;
        }
      } else {
        if (!display->set_format_idx(atoi(argv[2]))) {
          printf("Invalid format id\n");
          return -1;
        }
      }
      argv += 3;
      argc -= 3;
    } else if (strcmp(argv[0], "--grayscale") == 0) {
      for (auto& d : displays) {
        d.set_grayscale(true);
      }
      argv++;
      argc--;
    } else if (strcmp(argv[0], "--num-frames") == 0) {
      num_frames = atoi(argv[1]);
      argv += 2;
      argc -= 2;
    } else if (strcmp(argv[0], "--delay") == 0) {
      delay = atoi(argv[1]);
      argv += 2;
      argc -= 2;
    } else if (strcmp(argv[0], "--mediatek") == 0) {
      platform = ARM_MEDIATEK;
      argv += 1;
      argc -= 1;
    } else if (strcmp(argv[0], "--amlogic") == 0) {
      platform = ARM_AMLOGIC;
      argv += 1;
      argc -= 1;
    } else if (strcmp(argv[0], "--simple") == 0) {
      platform = SIMPLE;
      argv += 1;
      argc -= 1;
    } else {
      printf("Unrecognized argument \"%s\"\n", argv[0]);
      return -1;
    }
  }

  fbl::AllocChecker ac;
  if (platform == INTEL) {
    // Intel only supports 90/270 rotation for Y-tiled images, so enable it for testing.
    constexpr bool kIntelYTiling = true;

    // Color layer which covers all displays
    std::unique_ptr<ColorLayer> layer0 = fbl::make_unique_checked<ColorLayer>(&ac, displays);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    layers.push_back(std::move(layer0));

    // Layer which covers all displays and uses page flipping.
    std::unique_ptr<PrimaryLayer> layer1 = fbl::make_unique_checked<PrimaryLayer>(&ac, displays);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    layer1->SetLayerFlipping(true);
    layer1->SetAlpha(true, .75);
    layer1->SetIntelYTiling(kIntelYTiling);
    layers.push_back(std::move(layer1));

    // Layer which covers the left half of the of the first display
    // and toggles on and off every frame.
    std::unique_ptr<PrimaryLayer> layer2 =
        fbl::make_unique_checked<PrimaryLayer>(&ac, &displays[0]);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    layer2->SetImageDimens(displays[0].mode().horizontal_resolution / 2,
                           displays[0].mode().vertical_resolution);
    layer2->SetLayerToggle(true);
    layer2->SetScaling(true);
    layer2->SetIntelYTiling(kIntelYTiling);
    layers.push_back(std::move(layer2));

    // Intel only supports 3 layers, so add ifdef for quick toggling of the 3rd layer
#if 1
    // Layer which is smaller than the display and bigger than its image
    // and which animates back and forth across all displays and also
    // its src image and also rotates.
    std::unique_ptr<PrimaryLayer> layer3 = fbl::make_unique_checked<PrimaryLayer>(&ac, displays);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    // Width is the larger of disp_width/2, display_height/2, but we also need
    // to make sure that it's less than the smaller display dimension.
    uint32_t width = fbl::min(
        fbl::max(displays[0].mode().vertical_resolution / 2,
                 displays[0].mode().horizontal_resolution / 2),
        fbl::min(displays[0].mode().vertical_resolution, displays[0].mode().horizontal_resolution));
    uint32_t height = fbl::min(displays[0].mode().vertical_resolution / 2,
                               displays[0].mode().horizontal_resolution / 2);
    layer3->SetImageDimens(width * 2, height);
    layer3->SetDestFrame(width, height);
    layer3->SetSrcFrame(width, height);
    layer3->SetPanDest(true);
    layer3->SetPanSrc(true);
    layer3->SetRotates(true);
    layer3->SetIntelYTiling(kIntelYTiling);
    layers.push_back(std::move(layer3));
#else
    CursorLayer* layer4 = new CursorLayer(displays);
    layers.push_back(std::move(layer4));
#endif
  } else if (platform == ARM_MEDIATEK) {
    // Mediatek display test
    uint32_t width = displays[0].mode().horizontal_resolution;
    uint32_t height = displays[0].mode().vertical_resolution;
    std::unique_ptr<PrimaryLayer> layer1 = fbl::make_unique_checked<PrimaryLayer>(&ac, displays);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    layer1->SetAlpha(true, (float)0.2);
    layer1->SetImageDimens(width, height);
    layer1->SetSrcFrame(width / 2, height / 2);
    layer1->SetDestFrame(width / 2, height / 2);
    layer1->SetPanSrc(true);
    layer1->SetPanDest(true);
    layers.push_back(std::move(layer1));

    // Layer which covers the left half of the of the first display
    // and toggles on and off every frame.
    float alpha2 = (float)0.5;
    std::unique_ptr<PrimaryLayer> layer2 = fbl::make_unique_checked<PrimaryLayer>(&ac, displays);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    layer2->SetLayerFlipping(true);
    layer2->SetAlpha(true, alpha2);
    layers.push_back(std::move(layer2));

    float alpha3 = (float)0.2;
    std::unique_ptr<PrimaryLayer> layer3 = fbl::make_unique_checked<PrimaryLayer>(&ac, displays);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    layer3->SetAlpha(true, alpha3);
    layers.push_back(std::move(layer3));

    std::unique_ptr<PrimaryLayer> layer4 = fbl::make_unique_checked<PrimaryLayer>(&ac, displays);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    layer4->SetAlpha(true, (float)0.3);
    layers.push_back(std::move(layer4));
  } else if (platform == ARM_AMLOGIC) {
    // Amlogic display test
    std::unique_ptr<PrimaryLayer> layer1 = fbl::make_unique_checked<PrimaryLayer>(&ac, displays);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    layer1->SetLayerFlipping(true);
    layers.push_back(std::move(layer1));
  } else if (platform == SIMPLE) {
    // Simple display test
    bool mirrors = true;
    std::unique_ptr<PrimaryLayer> layer1 =
        fbl::make_unique_checked<PrimaryLayer>(&ac, displays, mirrors);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    layers.push_back(std::move(layer1));
  }

  printf("Initializing layers\n");
  for (auto& layer : layers) {
    if (!layer->Init(dc.get())) {
      printf("Layer init failed\n");
      return -1;
    }
  }

  for (auto& display : displays) {
    display.Init(dc.get());
  }

  printf("Starting rendering\n");
  for (int i = 0; i < num_frames; i++) {
    for (auto& layer : layers) {
      // Step before waiting, since not every layer is used every frame
      // so we won't necessarily need to wait.
      layer->StepLayout(i);

      if (!layer->WaitForReady()) {
        printf("Buffer failed to become free\n");
        return -1;
      }

      layer->clear_done();
      layer->SendLayout(dc.get());
    }

    for (unsigned i = 0; i < displays.size(); i++) {
      if (!update_display_layers(layers, displays[i], &display_layers[i])) {
        return -1;
      }
    }

    // This delay is used to skew the timing between vsync and ApplyConfiguration
    // in order to observe any tearing effects
    zx_nanosleep(zx_deadline_after(ZX_MSEC(delay)));
    if (!apply_config()) {
      return -1;
    }

    for (auto& layer : layers) {
      layer->Render(i);
    }

    zx_status_t status;
    while ((status = wait_for_vsync(layers)) == ZX_ERR_NEXT) {
    }
    ZX_ASSERT(status == ZX_OK);
  }

  printf("Done rendering\n");

  zx_handle_close(device_handle);

  return 0;
}
