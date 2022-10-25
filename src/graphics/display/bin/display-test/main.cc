// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.sysinfo/cpp/wire.h>
#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zircon-internal/align.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/pixelformat.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>

#include <fbl/algorithm.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>

#include "fidl/fuchsia.hardware.display/cpp/wire.h"
#include "fidl/fuchsia.hardware.display/cpp/wire_types.h"
#include "lib/ddk/driver.h"
#include "lib/fdio/directory.h"
#include "lib/fzl/vmo-mapper.h"
#include "src/graphics/display/testing/display.h"
#include "src/graphics/display/testing/utils.h"
#include "src/graphics/display/testing/virtual-layer.h"

namespace fhd = fuchsia_hardware_display;
namespace sysmem = fuchsia_sysmem;
namespace sysinfo = fuchsia_sysinfo;

using testing::display::ColorLayer;
using testing::display::CursorLayer;
using testing::display::Display;
using testing::display::PrimaryLayer;
using testing::display::VirtualLayer;

static zx_handle_t device_handle;
static fidl::WireSyncClient<fhd::Controller> dc;
static bool has_ownership;

constexpr uint64_t kEventId = 13;
constexpr uint32_t kCollectionId = 12;
uint64_t capture_id = 0;
zx::event client_event_;
fidl::WireSyncClient<sysmem::BufferCollection> collection_;
zx::vmo capture_vmo;

enum TestBundle {
  SIMPLE = 0,  // BUNDLE0
  FLIP,        // BUNDLE1
  INTEL,       // BUNDLE2
  BUNDLE3,
  BLANK,
  BUNDLE_COUNT,
};

enum Platforms {
  INTEL_PLATFORM = 0,
  AMLOGIC_PLATFORM,
  MEDIATEK_PLATFORM,
  AEMU_PLATFORM,
  QEMU_PLATFORM,
  UNKNOWN_PLATFORM,
  PLATFORM_COUNT,
};

Platforms platform = UNKNOWN_PLATFORM;
fbl::StringBuffer<sysinfo::wire::kBoardNameLen> board_name;

Platforms GetPlatform();
void Usage();

static bool bind_display(const char* controller, fbl::Vector<Display>* displays) {
  printf("Opening controller\n");
  zx::result provider = component::Connect<fhd::Provider>(controller);
  if (provider.is_error()) {
    printf("Failed to open display controller (%s)\n", provider.status_string());
    return false;
  }

  zx::result dc_endpoints = fidl::CreateEndpoints<fhd::Controller>();
  if (dc_endpoints.is_error()) {
    printf("Failed to create controller channel %d (%s)\n", dc_endpoints.error_value(),
           dc_endpoints.status_string());
    return false;
  }

  fidl::WireResult open_response =
      fidl::WireCall(provider.value())->OpenController(std::move(dc_endpoints->server));
  if (!open_response.ok()) {
    printf("Failed to call service handle: %s\n", open_response.FormatDescription().c_str());
    return false;
  }
  if (open_response.value().s != ZX_OK) {
    printf("Failed to open controller %d (%s)\n", open_response.value().s,
           zx_status_get_string(open_response.value().s));
    return false;
  }

  dc = fidl::WireSyncClient(std::move(dc_endpoints->client));

  class EventHandler : public fidl::WireSyncEventHandler<fhd::Controller> {
   public:
    EventHandler(fbl::Vector<Display>* displays, bool& has_ownership)
        : displays_(displays), has_ownership_(has_ownership) {}

    bool invalid_message() const { return invalid_message_; }

    void OnDisplaysChanged(fidl::WireEvent<fhd::Controller::OnDisplaysChanged>* event) override {
      for (size_t i = 0; i < event->added.count(); i++) {
        displays_->push_back(Display(event->added[i]));
      }
    }

    void OnVsync(fidl::WireEvent<fhd::Controller::OnVsync>* event) override {
      invalid_message_ = true;
    }

    void OnClientOwnershipChange(
        fidl::WireEvent<fhd::Controller::OnClientOwnershipChange>* event) override {
      has_ownership_ = event->has_ownership;
    }

   private:
    fbl::Vector<Display>* const displays_;
    bool& has_ownership_;
    bool invalid_message_ = false;
  };

  EventHandler event_handler(displays, has_ownership);
  while (displays->is_empty()) {
    printf("Waiting for display\n");
    if (!dc.HandleOneEvent(event_handler).ok() || event_handler.invalid_message()) {
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

bool import_gamma_tables(uint64_t id, float gamma) {
  fidl::Array<float, 256> r;
  fidl::Array<float, 256> g;
  fidl::Array<float, 256> b;
  generate_gamma_table(gamma, r.data());
  generate_gamma_table(gamma, g.data());
  generate_gamma_table(gamma, b.data());
  return dc->ImportGammaTable(id, r, g, b).ok();
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
    if (id != fhd::wire::kInvalidDispId) {
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
    if (!dc->SetDisplayLayers(display.id(), fidl::VectorView<uint64_t>::FromExternal(
                                                current_layers->data(), current_layers->size()))
             .ok()) {
      printf("Failed to set layers\n");
      return false;
    }
  }
  return true;
}

std::optional<fhd::wire::ConfigStamp> apply_config() {
  auto result = dc->CheckConfig(false);
  if (!result.ok()) {
    printf("Failed to make check call: %s\n", result.FormatDescription().c_str());
    return std::nullopt;
  }

  if (result.value().res != fhd::wire::ConfigResult::kOk) {
    printf("Config not valid (%d)\n", static_cast<uint32_t>(result.value().res));
    for (const auto& op : result.value().ops) {
      printf("Client composition op (display %ld, layer %ld): %hhu\n", op.display_id, op.layer_id,
             static_cast<uint8_t>(op.opcode));
    }
    return std::nullopt;
  }

  if (!dc->ApplyConfig().ok()) {
    printf("Apply failed\n");
    return std::nullopt;
  }

  auto config_stamp_result = dc->GetLatestAppliedConfigStamp();
  if (!config_stamp_result.ok()) {
    printf("GetLatestAppliedConfigStamp failed\n");
    return std::nullopt;
  }

  return config_stamp_result.value().stamp;
}

zx_status_t wait_for_vsync(fhd::wire::ConfigStamp expected_stamp) {
  class EventHandler : public fidl::WireSyncEventHandler<fhd::Controller> {
   public:
    explicit EventHandler(fhd::wire::ConfigStamp expected_stamp)
        : expected_stamp_(expected_stamp) {}

    zx_status_t status() const { return status_; }

    void OnDisplaysChanged(fidl::WireEvent<fhd::Controller::OnDisplaysChanged>* event) override {
      printf("Display disconnected\n");
      status_ = ZX_ERR_STOP;
    }

    void OnVsync(fidl::WireEvent<fhd::Controller::OnVsync>* event) override {
      // Acknowledge cookie if non-zero
      if (event->cookie) {
        // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
        (void)dc->AcknowledgeVsync(event->cookie);
      }

      if (event->applied_config_stamp.value >= expected_stamp_.value) {
        status_ = ZX_OK;
      } else {
        status_ = ZX_ERR_NEXT;
      }
    }

    void OnClientOwnershipChange(
        fidl::WireEvent<fhd::Controller::OnClientOwnershipChange>* event) override {
      has_ownership = event->has_ownership;
      status_ = ZX_ERR_NEXT;
    }

   private:
    fhd::wire::ConfigStamp expected_stamp_;
    zx_status_t status_ = ZX_OK;
  };

  EventHandler event_handler(expected_stamp);
  const fidl::Status status = dc.HandleOneEvent(event_handler);
  if (!status.ok()) {
    if (status.reason() == fidl::Reason::kUnexpectedMessage) {
      return ZX_ERR_STOP;
    }
    return status.status();
  }
  return event_handler.status();
}

zx_status_t set_minimum_rgb(uint8_t min_rgb) {
  auto resp = dc->SetMinimumRgb(min_rgb);
  return resp.status();
}

zx_status_t capture_setup() {
  // TODO(fxbug.dev/41413): Pull common image setup code into a library

  // First make sure capture is supported on this platform
  auto support_resp = dc->IsCaptureSupported();
  if (!support_resp.ok()) {
    printf("%s: %s\n", __func__, support_resp.FormatDescription().c_str());
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (!support_resp->value()->supported) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  // Import event used to get notified once capture is completed
  auto status = zx::event::create(0, &client_event_);
  if (status != ZX_OK) {
    printf("Could not create event %d\n", status);
    return status;
  }
  zx::event e2;
  status = client_event_.duplicate(ZX_RIGHT_SAME_RIGHTS, &e2);
  if (status != ZX_OK) {
    printf("Could not duplicate event %d\n", status);
    return status;
  }
  auto event_status = dc->ImportEvent(std::move(e2), kEventId);
  if (event_status.status() != ZX_OK) {
    printf("Could not import event: %s\n", event_status.FormatDescription().c_str());
    return event_status.status();
  }

  // get connection to sysmem
  zx::result sysmem_client = component::Connect<sysmem::Allocator>();
  if (sysmem_client.is_error()) {
    printf("Could not connect to sysmem Allocator %s\n", sysmem_client.status_string());
    return sysmem_client.status_value();
  }
  auto sysmem_allocator = fidl::WireSyncClient(std::move(sysmem_client.value()));

  // Create and import token
  zx::result token_endpoints = fidl::CreateEndpoints<sysmem::BufferCollectionToken>();
  if (token_endpoints.is_error()) {
    printf("Could not create token channel %d\n", token_endpoints.error_value());
    return token_endpoints.error_value();
  }
  auto token = fidl::WireSyncClient(std::move(token_endpoints->client));

  // pass token server to sysmem allocator
  fidl::WireResult alloc_status =
      sysmem_allocator->AllocateSharedCollection(std::move(token_endpoints->server));
  if (alloc_status.status() != ZX_OK) {
    printf("Could not pass token to sysmem allocator: %s\n",
           alloc_status.FormatDescription().c_str());
    return alloc_status.status();
  }

  // duplicate the token and pass to display driver
  zx::result token_dup_endpoints = fidl::CreateEndpoints<sysmem::BufferCollectionToken>();
  if (token_dup_endpoints.is_error()) {
    printf("Could not create duplicate token channel %d\n", token_dup_endpoints.error_value());
    return token_dup_endpoints.error_value();
  }
  fidl::WireSyncClient display_token(std::move(token_dup_endpoints->client));
  auto dup_res = token->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_dup_endpoints->server));
  if (dup_res.status() != ZX_OK) {
    printf("Could not duplicate token: %s\n", dup_res.FormatDescription().c_str());
    return dup_res.status();
  }
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)token->Sync();
  auto import_resp = dc->ImportBufferCollection(kCollectionId, display_token.TakeClientEnd());
  if (import_resp.status() != ZX_OK) {
    printf("Could not import token: %s\n", import_resp.FormatDescription().c_str());
    return import_resp.status();
  }

  // set buffer constraints
  fhd::wire::ImageConfig image_config = {};
  image_config.type = fhd::wire::kTypeCapture;
  auto constraints_resp = dc->SetBufferCollectionConstraints(kCollectionId, image_config);
  if (constraints_resp.status() != ZX_OK) {
    printf("Could not set capture constraints %s\n", constraints_resp.FormatDescription().c_str());
    return constraints_resp.status();
  }

  // setup our our constraints for buffer to be allocated
  zx::result collection_endpoints = fidl::CreateEndpoints<sysmem::BufferCollection>();
  if (collection_endpoints.is_error()) {
    printf("Could not create collection channel %d\n", collection_endpoints.error_value());
    return collection_endpoints.error_value();
  }
  // let's return token
  fidl::WireResult bind_resp = sysmem_allocator->BindSharedCollection(
      token.TakeClientEnd(), std::move(collection_endpoints->server));
  if (bind_resp.status() != ZX_OK) {
    printf("Could not bind to shared collection: %s\n", bind_resp.FormatDescription().c_str());
    return bind_resp.status();
  }

  // finally setup our constraints
  sysmem::wire::BufferCollectionConstraints constraints = {};
  constraints.usage.cpu = sysmem::wire::kCpuUsageReadOften | sysmem::wire::kCpuUsageWriteOften;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.ram_domain_supported = true;
  constraints.image_format_constraints_count = 1;
  sysmem::wire::ImageFormatConstraints& image_constraints = constraints.image_format_constraints[0];
  if (platform == AMLOGIC_PLATFORM) {
    image_constraints.pixel_format.type = sysmem::wire::PixelFormatType::kBgr24;
  } else {
    image_constraints.pixel_format.type = sysmem::wire::PixelFormatType::kBgra32;
  }
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] = sysmem::wire::ColorSpace{
      .type = sysmem::wire::ColorSpaceType::kSrgb,
  };
  image_constraints.min_coded_width = 0;
  image_constraints.max_coded_width = std::numeric_limits<uint32_t>::max();
  image_constraints.min_coded_height = 0;
  image_constraints.max_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints.min_bytes_per_row = 0;
  image_constraints.max_bytes_per_row = std::numeric_limits<uint32_t>::max();
  image_constraints.max_coded_width_times_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints.layers = 1;
  image_constraints.coded_width_divisor = 1;
  image_constraints.coded_height_divisor = 1;
  image_constraints.bytes_per_row_divisor = 1;
  image_constraints.start_offset_divisor = 1;
  image_constraints.display_width_divisor = 1;
  image_constraints.display_height_divisor = 1;

  collection_ = fidl::WireSyncClient(std::move(collection_endpoints->client));
  fidl::WireResult collection_resp = collection_->SetConstraints(true, constraints);
  if (collection_resp.status() != ZX_OK) {
    printf("Could not set buffer constraints: %s\n", collection_resp.FormatDescription().c_str());
    return collection_resp.status();
  }

  // wait for allocation
  auto wait_resp = collection_->WaitForBuffersAllocated();
  if (wait_resp.status() != ZX_OK) {
    printf("Wait for buffer allocation failed: %s\n", wait_resp.FormatDescription().c_str());
    return wait_resp.status();
  }

  capture_vmo = std::move(wait_resp.value().buffer_collection_info.buffers[0].vmo);
  // import image for capture
  fhd::wire::ImageConfig capture_cfg = {};  // will contain a handle
  auto importcap_resp = dc->ImportImageForCapture(capture_cfg, kCollectionId, 0);
  if (importcap_resp.status() != ZX_OK) {
    printf("Failed to start capture: %s\n", importcap_resp.FormatDescription().c_str());
    return importcap_resp.status();
  }
  if (importcap_resp->is_error()) {
    printf("Could not import image for capture %d\n", importcap_resp->error_value());
    return importcap_resp->error_value();
  }
  capture_id = importcap_resp->value()->image_id;
  return ZX_OK;
}

zx_status_t capture_start() {
  // start capture
  auto capstart_resp = dc->StartCapture(kEventId, capture_id);
  if (capstart_resp.status() != ZX_OK) {
    printf("Could not start capture: %s\n", capstart_resp.FormatDescription().c_str());
    return capstart_resp.status();
  }
  // wait for capture to complete
  uint32_t observed;
  auto event_res =
      client_event_.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(zx::sec(1)), &observed);
  if (event_res == ZX_OK) {
    client_event_.signal(ZX_EVENT_SIGNALED, 0);
  } else {
    printf("capture failed %d\n", event_res);
    return event_res;
  }
  return ZX_OK;
}

bool amlogic_capture_compare(void* capture_buf, void* actual_buf, size_t size, uint32_t height,
                             uint32_t width) {
  auto image_buf = std::make_unique<uint8_t[]>(size);
  std::memcpy(image_buf.get(), actual_buf, size);

  auto* imageptr = static_cast<uint8_t*>(image_buf.get());
  auto* captureptr = static_cast<uint8_t*>(capture_buf);

  // first fix endianess
  auto* tmpptr = reinterpret_cast<uint32_t*>(image_buf.get());
  for (size_t i = 0; i < size / 4; i++) {
    tmpptr[i] = be32toh(tmpptr[i]);
  }

  uint32_t capture_stride = ZX_ALIGN(width * ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_888), 64);
  uint32_t buffer_stride = ZX_ALIGN(width * ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_x888), 64);
  uint32_t buffer_width_bytes = width * ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_x888);
  uint32_t capture_width_bytes = width * ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_888);
  size_t buf_idx = 0;
  if (std::string_view(board_name.data(), board_name.size()).find("astro") !=
      std::string_view::npos) {
    // For Astro only:
    // Ignore last column. Has junk (hardware bug)
    // Ignoring last column, means there is a shift by one pixel.
    // Therefore, image_buffer should start from pixel 1 (i.e. 4th byte since x888) and
    // capture_buffer should end at width - 3 (i.e. 888)
    capture_width_bytes -= ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_888);
    buf_idx = ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_x888);
  }
  size_t cap_idx = 0;
  // Ignore first line. It <sometimes> contains junk (hardware bug).
  bool success = true;
  for (size_t h = 1; h < height; h++) {
    for (; cap_idx < capture_width_bytes && buf_idx < buffer_width_bytes;) {
      // skip the alpha channel
      if (((buf_idx) % 4) == 0) {
        buf_idx++;
        continue;
      }
      if (imageptr[h * buffer_stride + buf_idx] == captureptr[h * capture_stride + cap_idx]) {
        buf_idx++;
        cap_idx++;
        continue;
      }
      if (imageptr[h * buffer_stride + buf_idx] != 0 &&
          (imageptr[h * buffer_stride + buf_idx] == captureptr[h * capture_stride + cap_idx] + 1 ||
           imageptr[h * buffer_stride + buf_idx] == captureptr[h * capture_stride + cap_idx] - 1)) {
        buf_idx++;
        cap_idx++;
        continue;
      }
      success = false;
      printf("h:%zu, buf[%zu] = 0x%x, cap[%zu] = 0x%x\n", h, h * buffer_stride + buf_idx,
             imageptr[h * buffer_stride + buf_idx], h * capture_stride + cap_idx,
             captureptr[h * capture_stride + cap_idx]);
      break;
    }
    if (!success) {
      break;
    }
  }
  return success;
}

bool capture_compare(void* input_image_buf, uint32_t height, uint32_t width) {
  if (input_image_buf == nullptr) {
    printf("%s: null buf\n", __func__);
    return false;
  }

  fzl::VmoMapper mapped_capture_vmo;
  size_t capture_vmo_size;
  auto status = capture_vmo.get_size(&capture_vmo_size);
  if (status != ZX_OK) {
    printf("capture vmo get size failed %d\n", status);
    return status;
  }
  status =
      mapped_capture_vmo.Map(capture_vmo, 0, capture_vmo_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    printf("Could not map capture vmo %d\n", status);
    return status;
  }
  auto* ptr = reinterpret_cast<uint8_t*>(mapped_capture_vmo.start());
  zx_cache_flush(ptr, capture_vmo_size, ZX_CACHE_FLUSH_INVALIDATE);

  if (platform == AMLOGIC_PLATFORM) {
    return amlogic_capture_compare(mapped_capture_vmo.start(), input_image_buf, capture_vmo_size,
                                   height, width);
  }

  return !memcmp(input_image_buf, mapped_capture_vmo.start(), capture_vmo_size);
}

void capture_release() {
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)dc->ReleaseCapture(capture_id);
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)dc->ReleaseBufferCollection(kCollectionId);
}

void usage(void) {
  printf(
      "Usage: display-test [OPTIONS]\n\n"
      "--controller N           : open controller N [/dev/class/display-controller/N]\n"
      "--dump                   : print properties of attached display\n"
      "--mode-set D N           : Set Display D to mode N (use dump option for choices)\n"
      "--format-set D N         : Set Display D to format N (use dump option for choices)\n"
      "--grayscale              : Display images in grayscale mode (default off)\n"
      "--num-frames N           : Run test in N number of frames (default 120)\n"
      "                           N can be an integer or 'infinite'\n"
      "--delay N                : Add delay (ms) between Vsync complete and next configuration\n"
      "--capture                : Capture each display frame and verify\n"
      "--fgcolor 0xaarrggbb     : Set foreground color\n"
      "--bgcolor 0xaarrggbb     : Set background color\n"
      "--preoffsets x,y,z       : set preoffsets for color correction\n"
      "--postoffsets x,y,z      : set postoffsets for color correction\n"
      "--coeff c00,c01,...,,c22 : 3x3 coefficient matrix for color correction\n"
      "--enable-alpha           : Enable per-pixel alpha blending.\n"
      "--opacity o              : Set the opacity of the screen\n"
      "                           <o> is a value between [0 1] inclusive\n"
      "--enable-compression     : Enable framebuffer compression.\n"
      "--apply-config-once      : Apply configuration once in single buffer mode.\n"
      "--gamma g                : Enable Gamma Correction.\n"
      "                           <g> is the gamma correction value\n"
      "                           Valid values between [1.0 3.0]"
      "                           For Linear gamma, use g = 1\n"
      "--clamp-rgb c            : Set minimum RGB value [0 255].\n"
      "--configs-per-vsync n    : Number of configs applied per vsync\n"
      "--pattern pattern        : Image pattern to use - 'checkerboard' (default) or 'border'\n"
      "\nTest Modes:\n\n"
      "--bundle N       : Run test from test bundle N as described below\n\n"
      "                   bundle %d: Display a single pattern using single buffer\n"
      "                   bundle %d: Flip between two buffers to display a pattern\n"
      "                   bundle %d: Run the standard Intel-based display tests. This includes\n"
      "                             hardware composition of 1 color layer and 3 primary layers.\n"
      "                             The tests include alpha blending, translation, scaling\n"
      "                             and rotation\n"
      "                   bundle %d: 4 layer hardware composition with alpha blending\n"
      "                             and image translation\n"
      "                   bundle %d: Blank the screen and sleep for --num-frames.\n"
      "                   (default: bundle %d)\n\n"
      "--help           : Show this help message\n",
      SIMPLE, FLIP, INTEL, BUNDLE3, BLANK, INTEL);
}

Platforms GetPlatform() {
  zx::result sysinfo = component::Connect<sysinfo::SysInfo>();
  if (sysinfo.is_error()) {
    return UNKNOWN_PLATFORM;
  }

  auto result = fidl::WireCall<sysinfo::SysInfo>(sysinfo.value())->GetBoardName();
  if (!result.ok() || result.value().status != ZX_OK) {
    return UNKNOWN_PLATFORM;
  }

  board_name.Clear();
  board_name.Append(result.value().name.data(), result.value().name.size());

  printf("Found board %.*s\n", static_cast<int>(board_name.size()), result.value().name.data());

  auto board_name_cmp = std::string_view(board_name.data(), board_name.size());
  if (board_name_cmp == "x64" || board_name_cmp == "chromebook-x64" || board_name_cmp == "Eve" ||
      board_name_cmp.find("Nocturne") != std::string_view::npos ||
      board_name_cmp.find("NUC") != std::string_view::npos) {
    return INTEL_PLATFORM;
  }
  if (board_name_cmp.find("astro") != std::string_view::npos ||
      board_name_cmp.find("sherlock") != std::string_view::npos ||
      board_name_cmp.find("vim2") != std::string_view::npos ||
      board_name_cmp.find("vim3") != std::string_view::npos ||
      board_name_cmp.find("nelson") != std::string_view::npos ||
      board_name_cmp.find("luis") != std::string_view::npos) {
    return AMLOGIC_PLATFORM;
  }
  if (board_name_cmp.find("cleo") != std::string_view::npos ||
      board_name_cmp.find("mt8167s_ref") != std::string_view::npos) {
    return MEDIATEK_PLATFORM;
  }
  if (board_name_cmp.find("qemu") != std::string_view::npos ||
      board_name_cmp.find("Standard PC (Q35 + ICH9, 2009)") != std::string_view::npos) {
    return QEMU_PLATFORM;
  }
  return UNKNOWN_PLATFORM;
}

int main(int argc, const char* argv[]) {
  printf("Running display test\n");

  fbl::Vector<Display> displays;
  fbl::Vector<fbl::Vector<uint64_t>> display_layers;
  fbl::Vector<std::unique_ptr<VirtualLayer>> layers;
  std::optional<int32_t> num_frames = 120;  // default to 120 frames. std::nullopt means infinite
  int32_t delay = 0;
  bool capture = false;
  bool verify_capture = false;
  const char* controller = "/dev/class/display-controller/000";

  platform = GetPlatform();

  TestBundle testbundle;
  switch (platform) {
    case INTEL_PLATFORM:
      testbundle = INTEL;
      break;
    case AMLOGIC_PLATFORM:
      testbundle = FLIP;
      break;
    case MEDIATEK_PLATFORM:
      testbundle = BUNDLE3;
      break;
    default:
      testbundle = SIMPLE;
  }

  for (int i = 1; i < argc - 1; i++) {
    if (!strcmp(argv[i], "--controller")) {
      controller = argv[i + 1];
      break;
    }
  }

  if (!bind_display(controller, &displays)) {
    usage();
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

  testing::display::Image::Pattern image_pattern = testing::display::Image::Pattern::kCheckerboard;
  uint32_t fgcolor_rgba = 0xffff0000;  // red (default)
  uint32_t bgcolor_rgba = 0xffffffff;  // white (default)
  bool use_color_correction = false;
  float gamma = std::nanf("");
  int clamp_rgb = -1;

  testing::display::ColorCorrectionArgs color_correction_args;

  float alpha_val = std::nanf("");
  bool enable_alpha = false;
  bool enable_compression = false;
  bool apply_config_once = false;
  uint32_t configs_per_vsync = 1;

  while (argc) {
    if (strcmp(argv[0], "--dump") == 0) {
      for (auto& display : displays) {
        display.Dump();
      }
      return 0;
    }
    if (strcmp(argv[0], "--mode-set") == 0 || strcmp(argv[0], "--format-set") == 0) {
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
      if (strcmp(argv[1], "infinite") == 0) {
        num_frames = std::nullopt;
      } else {
        num_frames = atoi(argv[1]);
      }
      argv += 2;
      argc -= 2;
    } else if (strcmp(argv[0], "--controller") == 0) {
      // We already processed this, skip it.
      argv += 2;
      argc -= 2;
    } else if (strcmp(argv[0], "--delay") == 0) {
      delay = atoi(argv[1]);
      argv += 2;
      argc -= 2;
    } else if (strcmp(argv[0], "--bundle") == 0) {
      testbundle = static_cast<TestBundle>(atoi(argv[1]));
      if (testbundle >= BUNDLE_COUNT || testbundle < 0) {
        printf("Invalid test bundle selected\n");
        usage();
        return -1;
      }
      argv += 2;
      argc -= 2;
    } else if (strcmp(argv[0], "--capture") == 0) {
      capture = true;
      verify_capture = true;
      argv += 1;
      argc -= 1;
    } else if (strcmp(argv[0], "--gamma") == 0) {
      gamma = std::stof(argv[1]);
      if (gamma < 1 || gamma > 3) {
        usage();
        return -1;
      }
      argv += 2;
      argc -= 2;
    } else if (strcmp(argv[0], "--clamp-rgb") == 0) {
      clamp_rgb = atoi(argv[1]);
      if (clamp_rgb < 0 || clamp_rgb > 255) {
        usage();
        return -1;
      }
      argv += 2;
      argc -= 2;
    } else if (strcmp(argv[0], "--fgcolor") == 0) {
      fgcolor_rgba = static_cast<uint32_t>(strtoul(argv[1], nullptr, 16));
      argv += 2;
      argc -= 2;
    } else if (strcmp(argv[0], "--bgcolor") == 0) {
      bgcolor_rgba = static_cast<uint32_t>(strtoul(argv[1], nullptr, 16));
      argv += 2;
      argc -= 2;
    } else if (strcmp(argv[0], "--preoffsets") == 0) {
      sscanf(argv[1], "%f,%f,%f", &color_correction_args.preoffsets[0],
             &color_correction_args.preoffsets[1], &color_correction_args.preoffsets[2]);
      use_color_correction = true;
      argv += 2;
      argc -= 2;
    } else if (strcmp(argv[0], "--postoffsets") == 0) {
      sscanf(argv[1], "%f,%f,%f", &color_correction_args.postoffsets[0],
             &color_correction_args.postoffsets[1], &color_correction_args.postoffsets[2]);
      use_color_correction = true;
      argv += 2;
      argc -= 2;
    } else if (strcmp(argv[0], "--coeff") == 0) {
      sscanf(argv[1], "%f,%f,%f,%f,%f,%f,%f,%f,%f", &color_correction_args.coeff[0],
             &color_correction_args.coeff[1], &color_correction_args.coeff[2],
             &color_correction_args.coeff[3], &color_correction_args.coeff[4],
             &color_correction_args.coeff[5], &color_correction_args.coeff[6],
             &color_correction_args.coeff[7], &color_correction_args.coeff[8]);
      use_color_correction = true;
      argv += 2;
      argc -= 2;
    } else if (strcmp(argv[0], "--enable-alpha") == 0) {
      enable_alpha = true;
      argv += 1;
      argc -= 1;
    } else if (strcmp(argv[0], "--opacity") == 0) {
      enable_alpha = true;
      alpha_val = std::stof(argv[1]);
      if (alpha_val < 0 || alpha_val > 1) {
        printf("Invalid alpha value. Must be between 0 and 1\n");
        usage();
        return -1;
      }
      argv += 2;
      argc -= 2;
    } else if (strcmp(argv[0], "--enable-compression") == 0) {
      enable_compression = true;
      argv += 1;
      argc -= 1;
    } else if (strcmp(argv[0], "--apply-config-once") == 0) {
      apply_config_once = true;
      argv += 1;
      argc -= 1;
    } else if (strcmp(argv[0], "--configs-per-vsync") == 0) {
      configs_per_vsync = atoi(argv[1]);
      argv += 2;
      argc -= 2;
    } else if (strcmp(argv[0], "--pattern") == 0) {
      if (strcmp(argv[1], "checkerboard") == 0) {
        image_pattern = testing::display::Image::Pattern::kCheckerboard;
      } else if (strcmp(argv[1], "border") == 0) {
        image_pattern = testing::display::Image::Pattern::kBorder;
      } else {
        printf("Invalid image pattern \"%s\".\n", argv[1]);
        usage();
        return 0;
      }
      argv += 2;
      argc -= 2;

    } else if (strcmp(argv[0], "--help") == 0) {
      usage();
      return 0;
    } else {
      printf("Unrecognized argument \"%s\"\n", argv[0]);
      usage();
      return -1;
    }
  }

  if (use_color_correction) {
    for (auto& d : displays) {
      d.apply_color_correction(true);
    }
  }

  constexpr uint64_t gamma_id = 1;
  if (!std::isnan(gamma)) {
    if (!import_gamma_tables(gamma_id, gamma)) {
      printf("Error importing gamma table\n");
      return -1;
    }
  }

  if (capture && capture_setup() != ZX_OK) {
    printf("Could not setup capture\n");
    capture = false;
  }

  if (clamp_rgb != -1) {
    if (set_minimum_rgb(static_cast<uint8_t>(clamp_rgb)) != ZX_OK) {
      printf("Warning: RGB Clamping Not Supported!\n");
    }
  }

  // Call apply_config for each frame by default.
  std::optional<int32_t> max_apply_configs(num_frames);

  fbl::AllocChecker ac;
  if (testbundle == INTEL) {
    // Intel only supports 90/270 rotation for Y-tiled images, so enable it for testing.
    constexpr uint64_t kIntelYTilingModifier = fuchsia_sysmem::wire::kFormatModifierIntelI915YTiled;

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
    layer1->SetFormatModifier(kIntelYTilingModifier);
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
    layer2->SetFormatModifier(kIntelYTilingModifier);
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
    uint32_t width = std::min(
        std::max(displays[0].mode().vertical_resolution / 2,
                 displays[0].mode().horizontal_resolution / 2),
        std::min(displays[0].mode().vertical_resolution, displays[0].mode().horizontal_resolution));
    uint32_t height = std::min(displays[0].mode().vertical_resolution / 2,
                               displays[0].mode().horizontal_resolution / 2);
    layer3->SetImageDimens(width * 2, height);
    layer3->SetDestFrame(width, height);
    layer3->SetSrcFrame(width, height);
    layer3->SetPanDest(true);
    layer3->SetPanSrc(true);
    layer3->SetRotates(true);
    layer3->SetFormatModifier(kIntelYTilingModifier);
    layers.push_back(std::move(layer3));
#else
    CursorLayer* layer4 = new CursorLayer(displays);
    layers.push_back(std::move(layer4));
#endif
  } else if (testbundle == BUNDLE3) {
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
  } else if (testbundle == FLIP) {
    // Amlogic display test
    std::unique_ptr<PrimaryLayer> layer1 = fbl::make_unique_checked<PrimaryLayer>(
        &ac, displays, image_pattern, fgcolor_rgba, bgcolor_rgba);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    if (enable_alpha) {
      layer1->SetAlpha(true, alpha_val);
    }
    layer1->SetLayerFlipping(true);
    if (enable_compression) {
      layer1->SetFormatModifier(fuchsia_sysmem::wire::kFormatModifierArmAfbc16X16);
    }
    layers.push_back(std::move(layer1));
  } else if (testbundle == SIMPLE) {
    // Simple display test
    bool mirrors = true;
    std::unique_ptr<PrimaryLayer> layer1 = fbl::make_unique_checked<PrimaryLayer>(
        &ac, displays, image_pattern, fgcolor_rgba, bgcolor_rgba, mirrors);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    if (enable_compression) {
      layer1->SetFormatModifier(fuchsia_sysmem::wire::kFormatModifierArmAfbc16X16);
    }
    if (apply_config_once) {
      max_apply_configs = 1;
    }
    layers.push_back(std::move(layer1));
  } else if (testbundle == BLANK) {
    // 0 layers, applied one time
    max_apply_configs = 1;
  }

  printf("Initializing layers\n");
  for (auto& layer : layers) {
    if (!layer->Init(dc)) {
      printf("Layer init failed\n");
      return -1;
    }
  }

  for (auto& display : displays) {
    display.Init(dc, color_correction_args);
  }

  if (capture && layers.size() != 1) {
    printf("Capture disabled: verification only works for single-layer display tests\n");
    verify_capture = false;
  }

  printf("Starting rendering\n");
  if (capture) {
    printf("Capturing every frame. Verification is %s\n", verify_capture ? "enabled" : "disabled");
  }
  bool capture_result = true;
  for (int i = 0; !num_frames || i < num_frames; i++) {
    for (auto& layer : layers) {
      // Step before waiting, since not every layer is used every frame
      // so we won't necessarily need to wait.
      layer->StepLayout(i);

      if (!layer->WaitForReady()) {
        printf("Buffer failed to become free\n");
        return -1;
      }

      layer->clear_done();
      layer->SendLayout(dc);
    }

    for (unsigned i = 0; i < displays.size(); i++) {
      if (!update_display_layers(layers, displays[i], &display_layers[i])) {
        return -1;
      }
    }

    // This delay is used to skew the timing between vsync and ApplyConfiguration
    // in order to observe any tearing effects
    zx_nanosleep(zx_deadline_after(ZX_MSEC(delay)));

    // Check to see if we should set gamma correction
    if (!std::isnan(gamma)) {
      if (!dc->SetDisplayGammaTable(displays[0].id(), gamma_id).ok()) {
        printf("Could not set Gamma Table\n");
        return -1;
      }
    }

    fhd::wire::ConfigStamp expected_stamp = {.value = fhd::wire::kInvalidConfigStampValue};
    if (!max_apply_configs || i < max_apply_configs) {
      for (uint32_t cpv = 0; cpv < configs_per_vsync; cpv++) {
        auto maybe_expected_stamp = apply_config();
        if (!maybe_expected_stamp.has_value()) {
          return -1;
        } else {
          expected_stamp = *maybe_expected_stamp;
        }
      }
    }

    for (auto& layer : layers) {
      layer->Render(i);
    }

    zx_status_t status = ZX_OK;
    while (layers.size() != 0 && (status = wait_for_vsync(expected_stamp)) == ZX_ERR_NEXT) {
    }
    ZX_ASSERT(status == ZX_OK);
    if (capture) {
      // capture has been requested.
      status = capture_start();
      if (status != ZX_OK) {
        printf("Capture start failed %d\n", status);
        capture_release();
        capture = false;
        break;
      }
      if (verify_capture &&
          !capture_compare(layers[0]->GetCurrentImageBuf(), displays[0].mode().vertical_resolution,
                           displays[0].mode().horizontal_resolution)) {
        capture_result = false;
        break;
      }
    }
  }

  printf("Done rendering\n");

  if (capture) {
    printf("Capture completed\n");
    if (verify_capture) {
      if (capture_result) {
        printf("Capture Verification Passed\n");
      } else {
        printf("Capture Verification Failed!\n");
      }
    }
    capture_release();
  }
  zx_handle_close(device_handle);

  return 0;
}
