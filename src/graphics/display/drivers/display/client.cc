// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client.h"

#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/edid/edid.h>
#include <lib/image-format-llcpp/image-format-llcpp.h>
#include <lib/zx/channel.h>
#include <math.h>
#include <string.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <random>
#include <type_traits>
#include <utility>
#include <vector>

#include <ddk/debug.h>
#include <ddk/protocol/display/controller.h>
#include <ddk/trace/event.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>

#include "lib/fidl/llcpp/server.h"
#include "lib/zx/clock.h"
#include "lib/zx/time.h"

namespace fhd = llcpp::fuchsia::hardware::display;
namespace sysmem = llcpp::fuchsia::sysmem;

namespace {

constexpr uint32_t kFallbackHorizontalSizeMm = 160;
constexpr uint32_t kFallbackVerticalSizeMm = 90;

bool frame_contains(const frame_t& a, const frame_t& b) {
  return b.x_pos < a.width && b.y_pos < a.height && b.x_pos + b.width <= a.width &&
         b.y_pos + b.height <= a.height;
}

// We allocate some variable sized stack allocations based on the number of
// layers, so we limit the total number of layers to prevent blowing the stack.
static constexpr uint64_t kMaxLayers = 65536;
}  // namespace

namespace display {

void Client::ImportVmoImage(fhd::ImageConfig image_config, ::zx::vmo vmo, int32_t offset,
                            ImportVmoImageCompleter::Sync _completer) {
  if (!single_buffer_framebuffer_stride_) {
    _completer.Reply(ZX_ERR_INVALID_ARGS, 0);
    return;
  }

  image_t dc_image;
  dc_image.height = image_config.height;
  dc_image.width = image_config.width;
  dc_image.pixel_format = image_config.pixel_format;
  dc_image.type = image_config.type;

  zx::vmo dup_vmo;
  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
  if (status == ZX_OK) {
    status = controller_->dc()->ImportVmoImage(&dc_image, std::move(dup_vmo), offset);
  }

  if (status != ZX_OK) {
    _completer.Reply(status, 0);
    return;
  }
  if (status == ZX_OK) {
    fbl::AllocChecker ac;
    auto image = fbl::AdoptRef(
        new (&ac) Image(controller_, dc_image, std::move(vmo), single_buffer_framebuffer_stride_));
    if (!ac.check()) {
      controller_->dc()->ReleaseImage(&dc_image);
      _completer.Reply(ZX_ERR_NO_MEMORY, 0);
      return;
    }

    image->id = next_image_id_++;
    _completer.Reply(0, image->id);
    images_.insert(std::move(image));
  }
}

void Client::ImportImage(fhd::ImageConfig image_config, uint64_t collection_id, uint32_t index,
                         ImportImageCompleter::Sync _completer) {
  auto it = collection_map_.find(collection_id);
  if (it == collection_map_.end()) {
    _completer.Reply(ZX_ERR_INVALID_ARGS, 0);
    return;
  }
  sysmem::BufferCollection::SyncClient& collection = it->second.driver;

  auto check_status = collection.CheckBuffersAllocated();
  if (check_status.error() || check_status->status != ZX_OK) {
    _completer.Reply(ZX_ERR_SHOULD_WAIT, 0);
    return;
  }

  image_t dc_image = {};
  dc_image.height = image_config.height;
  dc_image.width = image_config.width;
  dc_image.pixel_format = image_config.pixel_format;
  dc_image.type = image_config.type;

  zx_status_t status = controller_->dc()->ImportImage(&dc_image, collection.channel().get(), index);
  if (status != ZX_OK) {
    _completer.Reply(status, 0);
    return;
  }

  auto release_image =
      fbl::MakeAutoCall([this, &dc_image]() { controller_->dc()->ReleaseImage(&dc_image); });
  zx::vmo vmo;
  uint32_t stride = 0;
  if (is_vc_) {
    ZX_ASSERT(it->second.kernel.channel());
    auto res = it->second.kernel.WaitForBuffersAllocated();
    if (res.error() || res->status != ZX_OK) {
      _completer.Reply(ZX_ERR_NO_MEMORY, 0);
      return;
    }
    sysmem::BufferCollectionInfo_2& info = res->buffer_collection_info;

    if (!info.settings.has_image_format_constraints || index >= info.buffer_count) {
      _completer.Reply(ZX_ERR_OUT_OF_RANGE, 0);
      return;
    }
    uint32_t minimum_row_bytes;
    if (!image_format::GetMinimumRowBytes(info.settings.image_format_constraints, dc_image.width,
                                          &minimum_row_bytes)) {
      _completer.Reply(ZX_ERR_INVALID_ARGS, 0);
      return;
    }
    vmo = std::move(info.buffers[index].vmo);
    stride = minimum_row_bytes / ZX_PIXEL_FORMAT_BYTES(dc_image.pixel_format);
  }

  fbl::AllocChecker ac;
  auto image = fbl::AdoptRef(new (&ac) Image(controller_, dc_image, std::move(vmo), stride));
  if (!ac.check()) {
    _completer.Reply(ZX_ERR_NO_MEMORY, 0);
    return;
  }

  auto image_id = next_image_id_++;
  image->id = image_id;
  release_image.cancel();
  images_.insert(std::move(image));

  _completer.Reply(0, image_id);
}

void Client::ReleaseImage(uint64_t image_id, ReleaseImageCompleter::Sync _completer) {
  auto image = images_.find(image_id);
  if (!image.IsValid()) {
    return;
  }

  if (CleanUpImage(&(*image))) {
    ApplyConfig();
  }
}

void Client::ImportEvent(::zx::event event, uint64_t id, ImportEventCompleter::Sync _completer) {
  if (id == INVALID_ID) {
    zxlogf(ERROR, "Cannot import events with an invalid ID #%i", INVALID_ID);
    TearDown();
  } else if (fences_.ImportEvent(std::move(event), id) != ZX_OK) {
    TearDown();
  }
}

void Client::ImportBufferCollection(uint64_t collection_id, ::zx::channel collection_token,
                                    ImportBufferCollectionCompleter::Sync _completer) {
  if (!sysmem_allocator_.channel()) {
    _completer.Reply(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  // TODO: Switch to .contains() when C++20.
  if (collection_map_.count(collection_id)) {
    _completer.Reply(ZX_ERR_INVALID_ARGS);
    return;
  }

  zx::channel vc_collection;

  // Make a second handle to represent the kernel's usage of the buffer as a
  // framebuffer, so we can set constraints and get VMOs for zx_framebuffer_set_range.
  if (is_vc_) {
    zx::channel vc_token_server, vc_token_client;
    zx::channel::create(0, &vc_token_server, &vc_token_client);
    if (sysmem::BufferCollectionToken::Call::Duplicate(zx::unowned_channel(collection_token),
                                                       UINT32_MAX, std::move(vc_token_server))
            .error()) {
      _completer.Reply(ZX_ERR_INTERNAL);
      return;
    }
    if (sysmem::BufferCollectionToken::Call::Sync(zx::unowned_channel(collection_token)).error()) {
      _completer.Reply(ZX_ERR_INTERNAL);
      return;
    }

    zx::channel collection_server;
    zx::channel::create(0, &collection_server, &vc_collection);
    if (sysmem_allocator_
            .BindSharedCollection(std::move(vc_token_client), std::move(collection_server))
            .error()) {
      _completer.Reply(ZX_ERR_INTERNAL);
      return;
    }
  }

  zx::channel collection_server, collection_client;
  zx::channel::create(0, &collection_server, &collection_client);
  if (sysmem_allocator_
          .BindSharedCollection(std::move(collection_token), std::move(collection_server))
          .error()) {
    _completer.Reply(ZX_ERR_INTERNAL);
    return;
  }

  collection_map_[collection_id] =
      Collections{sysmem::BufferCollection::SyncClient(std::move(collection_client)),
                  sysmem::BufferCollection::SyncClient(std::move(vc_collection))};
  _completer.Reply(ZX_OK);
}

void Client::ReleaseBufferCollection(uint64_t collection_id,
                                     ReleaseBufferCollectionCompleter::Sync _completer) {
  auto it = collection_map_.find(collection_id);
  if (it == collection_map_.end()) {
    return;
  }

  it->second.driver.Close();
  if (it->second.kernel.channel()) {
    it->second.kernel.Close();
  }
  collection_map_.erase(it);
}

void Client::SetBufferCollectionConstraints(
    uint64_t collection_id, fhd::ImageConfig config,
    SetBufferCollectionConstraintsCompleter::Sync _completer) {
  auto it = collection_map_.find(collection_id);
  if (it == collection_map_.end()) {
    _completer.Reply(ZX_ERR_INVALID_ARGS);
    return;
  }
  image_t dc_image;
  dc_image.height = config.height;
  dc_image.width = config.width;
  dc_image.pixel_format = config.pixel_format;
  dc_image.type = config.type;

  zx_status_t status = controller_->dc()->SetBufferCollectionConstraints(
      &dc_image, it->second.driver.channel().get());

  if (status == ZX_OK && is_vc_) {
    ZX_ASSERT(it->second.kernel.channel());

    // Constraints to be used with zx_framebuffer_set_range.
    sysmem::BufferCollectionConstraints constraints;
    constraints.usage.display = sysmem::displayUsageLayer;
    constraints.has_buffer_memory_constraints = true;
    sysmem::BufferMemoryConstraints& buffer_constraints = constraints.buffer_memory_constraints;
    buffer_constraints.min_size_bytes = 0;
    buffer_constraints.max_size_bytes = 0xffffffff;
    buffer_constraints.secure_required = false;
    buffer_constraints.ram_domain_supported = true;
    constraints.image_format_constraints_count = 1;
    sysmem::ImageFormatConstraints& image_constraints = constraints.image_format_constraints[0];
    switch (config.pixel_format) {
      case ZX_PIXEL_FORMAT_RGB_x888:
      case ZX_PIXEL_FORMAT_ARGB_8888:
        image_constraints.pixel_format.type = sysmem::PixelFormatType::BGRA32;
        image_constraints.pixel_format.has_format_modifier = true;
        image_constraints.pixel_format.format_modifier.value = sysmem::FORMAT_MODIFIER_LINEAR;
        break;
    }

    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0].type = sysmem::ColorSpaceType::SRGB;
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

    if (image_constraints.pixel_format.type != sysmem::PixelFormatType::INVALID) {
      _completer.Reply(it->second.kernel.SetConstraints(true, constraints).status());
      return;
    }
  }
  _completer.Reply(status);
}

void Client::ReleaseEvent(uint64_t id, ReleaseEventCompleter::Sync _completer) {
  fences_.ReleaseEvent(id);
}

void Client::CreateLayer(CreateLayerCompleter::Sync _completer) {
  if (layers_.size() == kMaxLayers) {
    _completer.Reply(ZX_ERR_NO_RESOURCES, 0);
    return;
  }

  fbl::AllocChecker ac;
  uint64_t layer_id = next_layer_id++;
  auto new_layer = fbl::make_unique_checked<Layer>(&ac, layer_id);
  if (!ac.check()) {
    --layer_id;
    _completer.Reply(ZX_ERR_NO_MEMORY, 0);
    return;
  }

  layers_.insert(std::move(new_layer));

  _completer.Reply(ZX_OK, layer_id);
}

void Client::DestroyLayer(uint64_t layer_id, DestroyLayerCompleter::Sync _completer) {
  auto layer = layers_.find(layer_id);
  if (!layer.IsValid()) {
    zxlogf(ERROR, "Tried to destroy invalid layer %ld", layer_id);
    TearDown();
    return;
  }
  if (layer->in_use()) {
    zxlogf(ERROR, "Destroyed layer %ld which was in use", layer_id);
    TearDown();
    return;
  }

  layers_.erase(layer_id);
}

void Client::ImportGammaTable(uint64_t gamma_table_id, ::fidl::Array<float, 256> r,
                              ::fidl::Array<float, 256> g, ::fidl::Array<float, 256> b,
                              ImportGammaTableCompleter::Sync _completer) {
  fbl::AllocChecker ac;
  auto gt = fbl::AdoptRef(new (&ac) GammaTables(r, g, b));
  if (!ac.check()) {
    zxlogf(ERROR, "%s failed!\n", __func__);
    return;
  }
  gamma_table_map_[gamma_table_id] = std::move(gt);
}

void Client::ReleaseGammaTable(uint64_t gamma_table_id,
                               ReleaseGammaTableCompleter::Sync _completer) {
  gamma_table_map_.erase(gamma_table_id);
}

void Client::SetDisplayMode(uint64_t display_id, fhd::Mode mode,
                            SetDisplayModeCompleter::Sync _completer) {
  auto config = configs_.find(display_id);
  if (!config.IsValid()) {
    return;
  }

  fbl::AutoLock lock(controller_->mtx());
  const fbl::Vector<edid::timing_params_t>* edid_timings;
  const display_params_t* params;
  controller_->GetPanelConfig(display_id, &edid_timings, &params);

  if (edid_timings) {
    for (auto timing : *edid_timings) {
      if (timing.horizontal_addressable == mode.horizontal_resolution &&
          timing.vertical_addressable == mode.vertical_resolution &&
          timing.vertical_refresh_e2 == mode.refresh_rate_e2) {
        Controller::PopulateDisplayMode(timing, &config->pending_.mode);
        pending_config_valid_ = false;
        config->display_config_change_ = true;
        return;
      }
    }
    zxlogf(ERROR, "Invalid display mode");
  } else {
    zxlogf(ERROR, "Failed to find edid when setting display mode");
  }

  TearDown();
}

void Client::SetDisplayColorConversion(uint64_t display_id, ::fidl::Array<float, 3> preoffsets,
                                       ::fidl::Array<float, 9> coefficients,
                                       ::fidl::Array<float, 3> postoffsets,
                                       SetDisplayColorConversionCompleter::Sync _completer) {
  auto config = configs_.find(display_id);
  if (!config.IsValid()) {
    return;
  }

  config->pending_.cc_flags = 0;
  if (!isnan(preoffsets[0])) {
    config->pending_.cc_flags |= COLOR_CONVERSION_PREOFFSET;
    memcpy(config->pending_.cc_preoffsets, preoffsets.data(), sizeof(preoffsets.data_));
    static_assert(sizeof(preoffsets) == sizeof(config->pending_.cc_preoffsets), "");
  }

  if (!isnan(coefficients[0])) {
    config->pending_.cc_flags |= COLOR_CONVERSION_COEFFICIENTS;
    memcpy(config->pending_.cc_coefficients, coefficients.data(), sizeof(coefficients.data_));
    static_assert(sizeof(coefficients) == sizeof(config->pending_.cc_coefficients), "");
  }

  if (!isnan(postoffsets[0])) {
    config->pending_.cc_flags |= COLOR_CONVERSION_POSTOFFSET;
    memcpy(config->pending_.cc_postoffsets, postoffsets.data(), sizeof(postoffsets.data_));
    static_assert(sizeof(postoffsets) == sizeof(config->pending_.cc_postoffsets), "");
  }

  config->display_config_change_ = true;
  pending_config_valid_ = false;
}

void Client::SetDisplayLayers(uint64_t display_id, ::fidl::VectorView<uint64_t> layer_ids,
                              SetDisplayLayersCompleter::Sync _completer) {
  auto config = configs_.find(display_id);
  if (!config.IsValid()) {
    return;
  }

  config->pending_layer_change_ = true;
  config->pending_layers_.clear();
  for (uint64_t i = layer_ids.count() - 1; i != UINT64_MAX; i--) {
    auto layer = layers_.find(layer_ids[i]);
    if (!layer.IsValid()) {
      zxlogf(ERROR, "Unknown layer %lu", layer_ids[i]);
      TearDown();
      return;
    }
    if (!layer->AddToConfig(&config->pending_layers_, /*z_index=*/static_cast<uint32_t>(i))) {
      zxlogf(ERROR, "Tried to reuse an in-use layer");
      TearDown();
      return;
    }
  }
  config->pending_.layer_count = static_cast<int32_t>(layer_ids.count());
  pending_config_valid_ = false;
}

void Client::SetDisplayGammaTable(uint64_t display_id, uint64_t gamma_table_id,
                                  SetDisplayGammaTableCompleter::Sync _completer) {
  auto config = configs_.find(display_id);
  if (!config.IsValid()) {
    return;
  }

  auto gamma_table = gamma_table_map_.find(gamma_table_id);
  if (gamma_table == gamma_table_map_.end()) {
    zxlogf(ERROR, "Invalid Gamma Table\n");
    TearDown();
    return;
  }

  config->pending_.gamma_table_present = true;
  config->pending_.gamma_red_list = gamma_table->second->Red();
  config->pending_.gamma_red_count = GammaTables::kTableSize;
  config->pending_.gamma_green_list = gamma_table->second->Green();
  config->pending_.gamma_green_count = GammaTables::kTableSize;
  config->pending_.gamma_blue_list = gamma_table->second->Blue();
  config->pending_.gamma_blue_count = GammaTables::kTableSize;

  // keep a reference of the table
  config->pending_gamma_table_ = gamma_table->second;
  config->display_config_change_ = true;
  pending_config_valid_ = false;
}

void Client::SetLayerPrimaryConfig(uint64_t layer_id, fhd::ImageConfig image_config,
                                   SetLayerPrimaryConfigCompleter::Sync /*_completer*/) {
  auto layer = layers_.find(layer_id);
  if (!layer.IsValid()) {
    zxlogf(ERROR, "SetLayerPrimaryConfig on invalid layer");
    TearDown();
    return;
  }

  layer->SetPrimaryConfig(image_config);
  pending_config_valid_ = false;
  // no Reply defined
}

void Client::SetLayerPrimaryPosition(uint64_t layer_id, fhd::Transform transform,
                                     fhd::Frame src_frame, fhd::Frame dest_frame,
                                     SetLayerPrimaryPositionCompleter::Sync /*_completer*/) {
  auto layer = layers_.find(layer_id);
  if (!layer.IsValid() || layer->pending_type() != LAYER_TYPE_PRIMARY) {
    zxlogf(ERROR, "SetLayerPrimaryPosition on invalid layer");
    TearDown();
    return;
  }
  if (transform > fhd::Transform::ROT_90_REFLECT_Y) {
    zxlogf(ERROR, "Invalid transform %hhu", static_cast<uint8_t>(transform));
    TearDown();
    return;
  }
  layer->SetPrimaryPosition(transform, src_frame, dest_frame);
  pending_config_valid_ = false;
  // no Reply defined
}

void Client::SetLayerPrimaryAlpha(uint64_t layer_id, fhd::AlphaMode mode, float val,
                                  SetLayerPrimaryAlphaCompleter::Sync /*_completer*/) {
  auto layer = layers_.find(layer_id);
  if (!layer.IsValid() || layer->pending_type() != LAYER_TYPE_PRIMARY) {
    zxlogf(ERROR, "SetLayerPrimaryAlpha on invalid layer");
    TearDown();
    return;
  }

  if (mode > fhd::AlphaMode::HW_MULTIPLY || (!isnan(val) && (val < 0 || val > 1))) {
    zxlogf(ERROR, "Invalid args %hhu %f", static_cast<uint8_t>(mode), val);
    TearDown();
    return;
  }
  layer->SetPrimaryAlpha(mode, val);
  pending_config_valid_ = false;
  // no Reply defined
}

void Client::SetLayerCursorConfig(uint64_t layer_id, fhd::ImageConfig image_config,
                                  SetLayerCursorConfigCompleter::Sync /*_completer*/) {
  auto layer = layers_.find(layer_id);
  if (!layer.IsValid()) {
    zxlogf(ERROR, "SetLayerCursorConfig on invalid layer");
    TearDown();
    return;
  }

  layer->SetCursorConfig(image_config);
  pending_config_valid_ = false;
  // no Reply defined
}

void Client::SetLayerCursorPosition(uint64_t layer_id, int32_t x, int32_t y,
                                    SetLayerCursorPositionCompleter::Sync /*_completer*/) {
  auto layer = layers_.find(layer_id);
  if (!layer.IsValid() || layer->pending_type() != LAYER_TYPE_CURSOR) {
    zxlogf(ERROR, "SetLayerCursorPosition on invalid layer");
    TearDown();
    return;
  }

  layer->SetCursorPosition(x, y);
  // no Reply defined
}

void Client::SetLayerColorConfig(uint64_t layer_id, uint32_t pixel_format,
                                 ::fidl::VectorView<uint8_t> color_bytes,
                                 SetLayerColorConfigCompleter::Sync /*_completer*/) {
  auto layer = layers_.find(layer_id);
  if (!layer.IsValid()) {
    zxlogf(ERROR, "SetLayerColorConfig on invalid layer");
    return;
  }

  if (color_bytes.count() != ZX_PIXEL_FORMAT_BYTES(pixel_format)) {
    zxlogf(ERROR, "SetLayerColorConfig with invalid color bytes");
    TearDown();
    return;
  }

  layer->SetColorConfig(pixel_format, std::move(color_bytes));
  pending_config_valid_ = false;
  // no Reply defined
}

void Client::SetLayerImage(uint64_t layer_id, uint64_t image_id, uint64_t wait_event_id,
                           uint64_t signal_event_id, SetLayerImageCompleter::Sync /*_completer*/) {
  auto layer = layers_.find(layer_id);
  if (!layer.IsValid()) {
    zxlogf(ERROR, "SetLayerImage ordinal with invalid layer %lu", layer_id);
    TearDown();
    return;
  }
  if (layer->pending_type() != LAYER_TYPE_PRIMARY && layer->pending_type() != LAYER_TYPE_CURSOR) {
    zxlogf(ERROR, "SetLayerImage ordinal with bad layer type");
    TearDown();
    return;
  }
  auto image = images_.find(image_id);
  if (!image.IsValid() || !image->Acquire()) {
    zxlogf(ERROR, "SetLayerImage ordinal with %s image", !image.IsValid() ? "invl" : "busy");
    TearDown();
    return;
  }
  const image_t* cur_image = layer->pending_image();
  if (!image->HasSameConfig(*cur_image)) {
    zxlogf(ERROR, "SetLayerImage with mismatch layer config");
    if (image.IsValid()) {
      image->DiscardAcquire();
    }
    TearDown();
    return;
  }

  layer->SetImage(image.CopyPointer(), wait_event_id, signal_event_id);
  // no Reply defined
}

void Client::CheckConfig(bool discard, CheckConfigCompleter::Sync _completer) {
  fhd::ConfigResult res;
  std::vector<fhd::ClientCompositionOp> ops;

  pending_config_valid_ = CheckConfig(&res, &ops);

  if (discard) {
    // Go through layers and release any pending resources they claimed
    for (auto& layer : layers_) {
      layer.DiscardChanges();
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

      config.pending_gamma_table_ = config.current_gamma_table_;
    }
    pending_config_valid_ = true;
  }

  _completer.Reply(res, ::fidl::unowned_vec(ops));
}

void Client::ApplyConfig(ApplyConfigCompleter::Sync /*_completer*/) {
  if (!pending_config_valid_) {
    pending_config_valid_ = CheckConfig(nullptr, nullptr);
    if (!pending_config_valid_) {
      zxlogf(INFO, "Tried to apply invalid config");
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
    for (auto& layer_node : display_config.pending_layers_) {
      if (!layer_node.layer->ResolvePendingImage(&fences_)) {
        TearDown();
        return;
      }
    }

    // If there was a layer change, update the current layers list.
    if (display_config.pending_layer_change_) {
      fbl::SinglyLinkedList<layer_node_t*> new_current;
      for (auto& layer_node : display_config.pending_layers_) {
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
    for (auto& layer_node : display_config.current_layers_) {
      layer_node.layer->ApplyChanges(display_config.current_.mode);
    }

    // TODO(fxbug.dev/54374): Controller needs to keep track of client switching and their applied
    // gamma table
    if (display_config.pending_gamma_table_ != nullptr &&
        (display_config.pending_gamma_table_ == display_config.current_gamma_table_)) {
      // no need to make client re-apply gamma table if it has already been aplied
      display_config.current_.apply_gamma_table = false;
    } else {
      display_config.current_gamma_table_ = display_config.pending_gamma_table_;
      display_config.current_.apply_gamma_table = true;
    }
  }
  // Overflow doesn't matter, since stamps only need to be unique until
  // the configuration is applied with vsync.
  client_apply_count_++;

  ApplyConfig();

  // no Reply defined
}

void Client::EnableVsync(bool enable, EnableVsyncCompleter::Sync /*_completer*/) {
  proxy_->EnableVsync(enable);
  // no Reply defined
}

void Client::SetVirtconMode(uint8_t mode, SetVirtconModeCompleter::Sync /*_completer*/) {
  if (!is_vc_) {
    zxlogf(ERROR, "Illegal non-virtcon ownership");
    TearDown();
    return;
  }
  controller_->SetVcMode(mode);
  // no Reply defined
}

void Client::GetSingleBufferFramebuffer(GetSingleBufferFramebufferCompleter::Sync _completer) {
  zx::vmo vmo;
  uint32_t stride = 0;
  zx_status_t status = controller_->dc()->GetSingleBufferFramebuffer(&vmo, &stride);
  single_buffer_framebuffer_stride_ = stride;
  _completer.Reply(status, std::move(vmo), stride);
}

void Client::IsCaptureSupported(IsCaptureSupportedCompleter::Sync _completer) {
  _completer.ReplySuccess(controller_->dc_capture() != nullptr);
}

void Client::ImportImageForCapture(fhd::ImageConfig image_config, uint64_t collection_id,
                                   uint32_t index,
                                   ImportImageForCaptureCompleter::Sync _completer) {
  // Ensure display driver supports/implements capture.
  if (controller_->dc_capture() == nullptr) {
    _completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  // Ensure a previously imported collection id is being used for import.
  auto it = collection_map_.find(collection_id);
  if (it == collection_map_.end()) {
    _completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  // Check whether buffer has already been allocated for the requested collection id.
  sysmem::BufferCollection::SyncClient& collection = it->second.driver;
  auto check_status = collection.CheckBuffersAllocated();
  if (check_status.error() || check_status->status != ZX_OK) {
    _completer.ReplyError(ZX_ERR_SHOULD_WAIT);
    return;
  }

  // capture_image will contain a handle that will be used by display driver to trigger
  // capture start/release.
  image_t capture_image = {};
  zx_status_t status = controller_->dc_capture()->ImportImageForCapture(
      collection.channel().get(), index, &capture_image.handle);
  if (status == ZX_OK) {
    auto release_image = fbl::MakeAutoCall([this, &capture_image]() {
      controller_->dc_capture()->ReleaseCapture(capture_image.handle);
    });

    fbl::AllocChecker ac;
    auto image = fbl::AdoptRef(new (&ac) Image(controller_, capture_image));
    if (!ac.check()) {
      _completer.ReplyError(ZX_ERR_NO_MEMORY);
      return;
    }
    image->id = next_capture_image_id++;
    _completer.ReplySuccess(image->id);
    release_image.cancel();
    capture_images_.insert(std::move(image));
  } else {
    _completer.ReplyError(status);
  }
}

void Client::StartCapture(uint64_t signal_event_id, uint64_t image_id,
                          StartCaptureCompleter::Sync _completer) {
  // Ensure display driver supports/implements capture.
  if (controller_->dc_capture() == nullptr) {
    _completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  // Don't start capture if one is in progress
  if (current_capture_image_ != INVALID_ID) {
    _completer.ReplyError(ZX_ERR_SHOULD_WAIT);
    return;
  }

  // Ensure we have a capture fence for the request signal event.
  auto signal_fence = fences_.GetFence(signal_event_id);
  if (signal_fence == nullptr) {
    _completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  // Ensure we are capturing into a valid image buffer
  auto image = capture_images_.find(image_id);
  if (!image.IsValid()) {
    zxlogf(ERROR, "Invalid Capture Image ID requested for capture");
    _completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  capture_fence_id_ = signal_event_id;
  auto status = controller_->dc_capture()->StartCapture(image->info().handle);
  if (status == ZX_OK) {
    fbl::AutoLock lock(controller_->mtx());
    proxy_->EnableCapture(true);
    _completer.ReplySuccess();
  } else {
    _completer.ReplyError(status);
  }

  // keep track of currently active capture image
  current_capture_image_ = image_id;  // Is this right?
}

void Client::ReleaseCapture(uint64_t image_id, ReleaseCaptureCompleter::Sync _completer) {
  // Ensure display driver supports/implements capture
  if (controller_->dc_capture() == nullptr) {
    _completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  // Ensure we are releasing a valid image buffer
  auto image = capture_images_.find(image_id);
  if (!image.IsValid()) {
    zxlogf(ERROR, "Invalid Capture Image ID requested for release");
    _completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  // Make sure we are not releasing an active capture.
  if (current_capture_image_ == image_id) {
    // we have an active capture. Release it when capture is completed
    zxlogf(WARNING, "Capture is active. Will release after capture is complete");
    pending_capture_release_image_ = current_capture_image_;
  } else {
    // release image now
    capture_images_.erase(image);
  }
  _completer.ReplySuccess();
}

void Client::SetMinimumRgb(uint8_t minimum_rgb, SetMinimumRgbCompleter::Sync _completer) {
  if (controller_->dc_clamp_rgb() == nullptr) {
    _completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  if (!is_owner_) {
    _completer.ReplyError(ZX_ERR_NOT_CONNECTED);
    return;
  }
  auto status = controller_->dc_clamp_rgb()->SetMinimumRgb(minimum_rgb);
  if (status == ZX_OK) {
    client_minimum_rgb_ = minimum_rgb;
    _completer.ReplySuccess();
  } else {
    _completer.ReplyError(status);
  }
}

bool Client::CheckConfig(fhd::ConfigResult* res, std::vector<fhd::ClientCompositionOp>* ops) {
  const display_config_t* configs[configs_.size()];
  layer_t* layers[layers_.size()];
  uint32_t layer_cfg_results[layers_.size()];
  uint32_t* display_layer_cfg_results[configs_.size()];
  memset(layer_cfg_results, 0, layers_.size() * sizeof(uint32_t));

  if (res && ops) {
    *res = fhd::ConfigResult::OK;
    ops->clear();
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
    if (res) {
      *res = fhd::ConfigResult::INVALID_CONFIG;
    }
    // If the config is invalid, there's no point in sending it to the impl driver.
    return false;
  }

  size_t layer_cfg_results_count;
  uint32_t display_cfg_result = controller_->dc()->CheckConfiguration(
      configs, config_idx, display_layer_cfg_results, &layer_cfg_results_count);

  if (display_cfg_result != CONFIG_DISPLAY_OK) {
    if (res) {
      *res = display_cfg_result == CONFIG_DISPLAY_TOO_MANY
                 ? fhd::ConfigResult::TOO_MANY_DISPLAYS
                 : fhd::ConfigResult::UNSUPPORTED_DISPLAY_MODES;
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
  } else if (!(res && ops)) {
    return false;
  }
  *res = fhd::ConfigResult::UNSUPPORTED_CONFIG;

  constexpr uint32_t kAllErrors = (CLIENT_GAMMA << 1) - 1;

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
          fhd::ClientCompositionOp op{
              .display_id = display_config.id,
              .layer_id = layer_node.layer->id,
              .opcode = static_cast<fhd::ClientCompositionOpcode>(i),

          };
          ops->push_back(std::move(op));
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

    for (auto& layer_node : display_config.current_layers_) {
      Layer* layer = layer_node.layer;
      const bool activated = layer->ActivateLatestReadyImage();
      if (activated && layer->current_image()) {
        display_config.pending_apply_layer_change_ = true;
      }

      if (is_vc_) {
        auto fb = layer->current_image();
        if (fb) {
          // If the virtcon is displaying an image, set it as the kernel's framebuffer
          // vmo. If the virtcon is displaying images on multiple displays, this ends
          // executing multiple times, but the extra work is okay since the virtcon
          // shouldn't be flipping images.
          console_fb_display_id_ = display_config.id;

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
        if (layer->current_image() == nullptr) {
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

  zx_status_t status = fhd::Controller::SendOnClientOwnershipChangeEvent(
      zx::unowned_channel(server_handle_), is_owner);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Error writing remove message %d", status);
  }

  ApplyConfig();
}

void Client::OnDisplaysChanged(const uint64_t* displays_added, size_t added_count,
                               const uint64_t* displays_removed, size_t removed_count) {
  ZX_DEBUG_ASSERT(controller_->current_thread_is_loop());

  size_t actual_removed_count = 0;
  size_t actual_added_count = 0;

  for (unsigned i = 0; i < removed_count; i++) {
    // TODO(stevensd): Delayed removal can cause conflicts if the driver reuses
    // display ids. Move display id generation into the core driver.
    if (configs_.find(displays_removed[i]).IsValid()) {
      actual_removed_count++;
    }
  }

  controller_->AssertMtxAliasHeld(controller_->mtx());
  for (unsigned i = 0; i < added_count; i++) {
    fbl::AllocChecker ac;
    auto config = fbl::make_unique_checked<DisplayConfig>(&ac);
    if (!ac.check()) {
      zxlogf(WARNING, "Out of memory when processing hotplug");
      continue;
    }

    config->id = displays_added[i];

    if (!controller_->GetSupportedPixelFormats(config->id, &config->pixel_formats_)) {
      zxlogf(WARNING, "Failed to get pixel formats when processing hotplug");
      continue;
    }

    if (!controller_->GetCursorInfo(config->id, &config->cursor_infos_)) {
      zxlogf(WARNING, "Failed to get cursor info when processing hotplug");
      continue;
    }

    const fbl::Vector<edid::timing_params_t>* edid_timings;
    const display_params_t* params;
    if (!controller_->GetPanelConfig(config->id, &edid_timings, &params)) {
      // This can only happen if the display was already disconnected.
      zxlogf(WARNING, "No config when adding display");
      continue;
    }
    actual_added_count++;

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
  std::vector<fhd::Info> coded_configs;
  coded_configs.reserve(added_count);

  // Hang on to modes values until we send the message.
  std::vector<std::vector<fhd::Mode>> modes_vector;

  for (unsigned i = 0; i < added_count; i++) {
    auto config = configs_.find(displays_added[i]);
    if (!config.IsValid()) {
      continue;
    }

    fhd::Info info;
    info.id = config->id;

    const fbl::Vector<edid::timing_params>* edid_timings;
    const display_params_t* params;
    controller_->GetPanelConfig(config->id, &edid_timings, &params);
    std::vector<fhd::Mode> modes;
    if (edid_timings) {
      modes.reserve(edid_timings->size());
      for (auto timing : *edid_timings) {
        fhd::Mode mode{
            .horizontal_resolution = timing.horizontal_addressable,
            .vertical_resolution = timing.vertical_addressable,
            .refresh_rate_e2 = timing.vertical_refresh_e2,
        };
        modes.push_back(std::move(mode));
      }
    } else {
      modes.reserve(1);
      fhd::Mode mode{
          .horizontal_resolution = params->width,
          .vertical_resolution = params->height,
          .refresh_rate_e2 = params->refresh_rate_e2,
      };
      modes.push_back(std::move(mode));
    }
    modes_vector.emplace_back(std::move(modes));
    info.modes = fidl::unowned_vec(modes_vector.back());

    static_assert(sizeof(zx_pixel_format_t) == sizeof(int32_t), "Bad pixel format size");
    info.pixel_format = fidl::unowned_vec(config->pixel_formats_);

    static_assert(offsetof(cursor_info_t, width) == offsetof(fhd::CursorInfo, width), "Bad struct");
    static_assert(offsetof(cursor_info_t, height) == offsetof(fhd::CursorInfo, height),
                  "Bad struct");
    static_assert(offsetof(cursor_info_t, format) == offsetof(fhd::CursorInfo, pixel_format),
                  "Bad struct");
    static_assert(sizeof(cursor_info_t) <= sizeof(fhd::CursorInfo), "Bad size");
    info.cursor_configs = fidl::VectorView<fhd::CursorInfo>(
        fidl::unowned_ptr((fhd::CursorInfo*)config->cursor_infos_.data()),
        config->cursor_infos_.size());

    const char* manufacturer_name = "";
    const char* monitor_name = "";
    const char* monitor_serial = "";
    if (!controller_->GetDisplayIdentifiers(displays_added[i], &manufacturer_name, &monitor_name,
                                            &monitor_serial)) {
      zxlogf(ERROR, "Failed to get display identifiers");
      ZX_DEBUG_ASSERT(false);
    }

    info.using_fallback_size = false;
    if (!controller_->GetDisplayPhysicalDimensions(displays_added[i], &info.horizontal_size_mm,
                                                   &info.vertical_size_mm)) {
      zxlogf(ERROR, "Failed to get display physical dimensions");
      ZX_DEBUG_ASSERT(false);
    }
    if (info.horizontal_size_mm == 0 || info.vertical_size_mm == 0) {
      info.horizontal_size_mm = kFallbackHorizontalSizeMm;
      info.vertical_size_mm = kFallbackVerticalSizeMm;
      info.using_fallback_size = true;
    }

    info.manufacturer_name = fidl::unowned_str(manufacturer_name, strlen(manufacturer_name));
    info.monitor_name = fidl::unowned_str(monitor_name, strlen(monitor_name));
    info.monitor_serial = fidl::unowned_str(monitor_serial, strlen(monitor_serial));

    coded_configs.push_back(std::move(info));
  }

  std::vector<uint64_t> removed_ids;
  removed_ids.reserve(removed_count);

  for (unsigned i = 0; i < removed_count; i++) {
    auto display = configs_.erase(displays_removed[i]);
    if (display) {
      display->pending_layers_.clear();
      display->current_layers_.clear();
      removed_ids.push_back(display->id);
    }
  }

  if (!coded_configs.empty() || !removed_ids.empty()) {
    zx_status_t status = fhd::Controller::SendOnDisplaysChangedEvent(
        zx::unowned_channel(server_handle_), fidl::unowned_vec(coded_configs),
        fidl::unowned_vec(removed_ids));
    if (status != ZX_OK) {
      zxlogf(ERROR, "Error writing remove message %d", status);
    }
  }
}

void Client::OnFenceFired(FenceReference* fence) {
  bool new_image_ready = false;
  for (auto& layer : layers_) {
    image_node_t* waiting;
    list_for_every_entry (&layer.waiting_images_, waiting, image_node_t, link) {
      new_image_ready |= waiting->self->OnFenceReady(fence);
    }
  }
  if (new_image_ready) {
    ApplyConfig();
  }
}

void Client::CaptureCompleted() {
  auto signal_fence = fences_.GetFence(capture_fence_id_);
  if (signal_fence != nullptr) {
    signal_fence->Signal();
  }

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

  // make sure we stop vsync messages from this client since server_channel has already
  // been closed by fidl server
  proxy_->EnableVsync(false);

  server_handle_ = ZX_HANDLE_INVALID;

  CleanUpImage(nullptr);
  CleanUpCaptureImage();

  fences_.Clear();

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
    current_config_change |= layer.CleanUpImage(image);
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

void Client::AcknowledgeVsync(uint64_t cookie, AcknowledgeVsyncCompleter::Sync _completer) {
  acked_cookie_ = cookie;
  zxlogf(TRACE, "Cookie %ld Acked\n", cookie);
}

zx_status_t Client::Init(zx::channel server_channel) {
  zx_status_t status;

  server_handle_ = server_channel.get();

  fidl::OnUnboundFn<Client> cb = [](Client* client, fidl::UnbindInfo info, zx::channel ch) {
    sync_completion_signal(client->fidl_unbound());
    // DdkRelease will cancel the FIDL binding before destroying the client. Therefore, we should
    // TearDown() so that no further tasks are scheduled on the controller loop.
    switch (info.reason) {
      case fidl::UnbindInfo::kUnbind:
      case fidl::UnbindInfo::kClose:
        break;
      default:
        client->TearDown();
    }
    // fidl_channel_ is the zx::channel alias of server_channel_. Hold on to it so that
    // OnDisplayVsync works until this client is destroyed.
    client->fidl_channel_ = std::move(ch);
  };

  auto res = fidl::BindServer(controller_->loop().dispatcher(), std::move(server_channel), this,
                              std::move(cb));
  if (!res.is_ok()) {
    zxlogf(ERROR, "%s: Failed to bind to FIDL Server (%d)", __func__, res.error());
    return res.error();
  }
  // keep a copy of fidl binding so we can safely unbind from it during shutdown
  fidl_binding_ = res.take_value();

  zx::channel sysmem_allocator_request, sysmem_allocator_client;
  zx::channel::create(0, &sysmem_allocator_request, &sysmem_allocator_client);
  status = controller_->dc()->GetSysmemConnection(std::move(sysmem_allocator_request));
  if (status != ZX_OK) {
    // Not a fatal error, but BufferCollection functions won't work.
    // TODO(fxbug.dev/33157) TODO: Fail creation once all drivers implement this.
    zxlogf(ERROR, "GetSysmemConnection failed (continuing) - status: %d", status);
  } else {
    sysmem_allocator_ = sysmem::Allocator::SyncClient(std::move(sysmem_allocator_client));
  }

  return ZX_OK;
}

Client::Client(Controller* controller, ClientProxy* proxy, bool is_vc, uint32_t client_id)
    : controller_(controller),
      proxy_(proxy),
      is_vc_(is_vc),
      id_(client_id),
      fences_(controller->loop().dispatcher(), fit::bind_member(this, &Client::OnFenceFired)) {}

Client::Client(Controller* controller, ClientProxy* proxy, bool is_vc, uint32_t client_id,
               zx::channel server_channel)
    : controller_(controller),
      proxy_(proxy),
      is_vc_(is_vc),
      id_(client_id),
      server_channel_(std::move(server_channel)),
      server_handle_(server_channel_.get()),
      fences_(controller->loop().dispatcher(), fit::bind_member(this, &Client::OnFenceFired)) {}

Client::~Client() { ZX_DEBUG_ASSERT(server_handle_ == ZX_HANDLE_INVALID); }

void ClientProxy::SetOwnership(bool is_owner) {
  fbl::AllocChecker ac;
  auto task = fbl::make_unique_checked<async::Task>(&ac);
  if (!ac.check()) {
    zxlogf(WARNING, "Failed to allocate set ownership task");
    return;
  }
  task->set_handler([this, client_handler = &handler_, is_owner](
                        async_dispatcher_t* /*dispatcher*/, async::Task* task, zx_status_t status) {
    if (status == ZX_OK && client_handler->IsValid()) {
      client_handler->SetOwnership(is_owner);
    }
    // update client_scheduled_tasks_
    mtx_lock(&this->task_mtx_);
    auto it = std::find_if(client_scheduled_tasks_.begin(), client_scheduled_tasks_.end(),
                           [&](std::unique_ptr<async::Task>& t) { return t.get() == task; });
    // Current task must have been added to the list.
    ZX_DEBUG_ASSERT(it != client_scheduled_tasks_.end());
    client_scheduled_tasks_.erase(it);
    mtx_unlock(&this->task_mtx_);
  });
  mtx_lock(&task_mtx_);
  if (task->Post(controller_->loop().dispatcher()) == ZX_OK) {
    client_scheduled_tasks_.push_back(std::move(task));
  }
  mtx_unlock(&task_mtx_);
}

void ClientProxy::OnDisplaysChanged(const uint64_t* displays_added, size_t added_count,
                                    const uint64_t* displays_removed, size_t removed_count) {
  handler_.OnDisplaysChanged(displays_added, added_count, displays_removed, removed_count);
}

void ClientProxy::ReapplySpecialConfigs() {
  ZX_DEBUG_ASSERT(mtx_trylock(controller_->mtx()) == thrd_busy);
  if (controller_->dc_clamp_rgb()) {
    controller_->dc_clamp_rgb()->SetMinimumRgb(handler_.GetMinimumRgb());
  }
}

void ClientProxy::ReapplyConfig() {
  fbl::AllocChecker ac;
  auto task = fbl::make_unique_checked<async::Task>(&ac);
  if (!ac.check()) {
    zxlogf(WARNING, "Failed to reapply config");
    return;
  }

  task->set_handler([this, client_handler = &handler_](async_dispatcher_t* /*dispatcher*/,
                                                       async::Task* task, zx_status_t status) {
    if (status == ZX_OK && client_handler->IsValid()) {
      client_handler->ApplyConfig();
    }
    // update client_scheduled_tasks_
    mtx_lock(&this->task_mtx_);
    auto it = std::find_if(client_scheduled_tasks_.begin(), client_scheduled_tasks_.end(),
                           [&](std::unique_ptr<async::Task>& t) { return t.get() == task; });
    // Current task must have been added to the list.
    ZX_DEBUG_ASSERT(it != client_scheduled_tasks_.end());
    client_scheduled_tasks_.erase(it);
    mtx_unlock(&this->task_mtx_);
  });
  mtx_lock(&task_mtx_);
  if (task->Post(controller_->loop().dispatcher()) == ZX_OK) {
    client_scheduled_tasks_.push_back(std::move(task));
  }
  mtx_unlock(&task_mtx_);
}

zx_status_t ClientProxy::OnCaptureComplete() {
  ZX_DEBUG_ASSERT(mtx_trylock(controller_->mtx()) == thrd_busy);
  fbl::AutoLock l(&mtx_);
  if (enable_capture_) {
    handler_.CaptureCompleted();
  }
  enable_capture_ = false;
  return ZX_OK;
}

zx_status_t ClientProxy::OnDisplayVsync(uint64_t display_id, zx_time_t timestamp,
                                        uint64_t* image_ids, size_t count) {
  ZX_DEBUG_ASSERT(mtx_trylock(controller_->mtx()) == thrd_busy);

  {
    fbl::AutoLock l(&mtx_);
    if (!enable_vsync_) {
      return ZX_ERR_NOT_SUPPORTED;
    }
  }
  zx_status_t status = ZX_OK;

  uint64_t cookie = 0;
  if (number_of_vsyncs_sent_ >= (kVsyncMessagesWatermark - 1)) {
    // Number of  vsync events sent exceed the watermark level.
    // Check to see if client has been notified already that acknowledgement is needed
    if (!acknowledge_request_sent_) {
      // We have not sent a (new) cookie to client for acknowledgement. Let's do it now
      // First increment cookie sequence
      cookie_sequence_++;
      // Generate new cookie by xor'ing initial cookie with sequence number.
      cookie = initial_cookie_ ^ cookie_sequence_;
    } else {
      // Client has already been notified. let's check if client has acknowledged it
      ZX_DEBUG_ASSERT(last_cookie_sent_ != 0);
      if (handler_.LatestAckedCookie() == last_cookie_sent_) {
        // Client has acknowledged cookie. Reset vsync tracking states
        number_of_vsyncs_sent_ = 0;
        acknowledge_request_sent_ = false;
        last_cookie_sent_ = 0;
      }
    }
  }

  if (number_of_vsyncs_sent_ >= kMaxVsyncMessages) {
    // We have reached/exceeded maximum allowed vsyncs without any acknowledgement. At this point,
    // start storing them
    zxlogf(TRACE, "Vsync not sent due to none acknowledgment.\n");
    ZX_DEBUG_ASSERT(cookie == 0);  // cookie should be zero!
    if (buffered_vsync_messages_.full()) {
      buffered_vsync_messages_.pop();  // discard
    }
    vsync_msg_t v = {
        .display_id = display_id,
        .timestamp = timestamp,
        .count = count,
    };
    ZX_DEBUG_ASSERT(count <= kMaxImageHandles);
    for (uint64_t i = 0; i < count; i++) {
      v.image_ids[i] = image_ids[i];
    }
    buffered_vsync_messages_.push(v);
    return ZX_ERR_BAD_STATE;
  }

  auto cleanup = fbl::MakeAutoCall([&]() {
    if (cookie) {
      cookie_sequence_--;
    }
    // Make sure status is not ZX_ERR_BAD_HANDLE, otherwise, depending on
    // policy setting channel write will crash
    ZX_DEBUG_ASSERT(status != ZX_ERR_BAD_HANDLE);
    if (status == ZX_ERR_NO_MEMORY) {
      total_oom_errors_++;
      // OOM errors are most likely not recoverable. Print the error message
      // once every kChannelErrorPrintFreq cycles
      if (chn_oom_print_freq_++ == 0) {
        zxlogf(ERROR, "Failed to send vsync event (OOM) (total occurrences: %lu)",
               total_oom_errors_);
      }
      if (chn_oom_print_freq_ >= kChannelOomPrintFreq) {
        chn_oom_print_freq_ = 0;
      }
    } else {
      zxlogf(WARNING, "Failed to send vsync event %d", status);
    }
  });

  // Send buffered vsync events before sending the latest
  while (!buffered_vsync_messages_.empty()) {
    vsync_msg_t v = buffered_vsync_messages_.front();
    buffered_vsync_messages_.pop();
    status = fhd::Controller::SendOnVsyncEvent(
        zx::unowned_channel(server_channel_), v.display_id, v.timestamp,
        fidl::VectorView(fidl::unowned_ptr(v.image_ids), v.count), 0);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to send all buffered vsync messages %d\n", status);
      return status;
    }
    number_of_vsyncs_sent_++;
  }

  // Send the latest vsync event
  status = fhd::Controller::SendOnVsyncEvent(
      zx::unowned_channel(server_channel_), display_id, timestamp,
      fidl::VectorView(fidl::unowned_ptr(image_ids), count), cookie);
  if (status != ZX_OK) {
    return status;
  }

  // Update vsync tracking states
  if (cookie) {
    acknowledge_request_sent_ = true;
    last_cookie_sent_ = cookie;
  }
  number_of_vsyncs_sent_++;
  cleanup.cancel();
  return ZX_OK;
}  // namespace display

void ClientProxy::OnClientDead() { controller_->OnClientDead(this); }

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
  zxlogf(INFO, "DdkClose");
  CloseOnControllerLoop();
  return ZX_OK;
}

void ClientProxy::DdkUnbind(ddk::UnbindTxn txn) {
  zxlogf(INFO, "ClientProxy::DdkUnbind");
  CloseOnControllerLoop();
  txn.Reply();
}

void ClientProxy::DdkRelease() {
  // Schedule release on controller loop. This way, we can safely cancel any pending tasks before
  // releasing the client.
  auto* task = new async::Task();
  task->set_handler(
      [this](async_dispatcher_t* /*dispatcher*/, async::Task* task, zx_status_t /*status*/) {
        mtx_lock(&this->task_mtx_);
        for (auto& t : this->client_scheduled_tasks_) {
          t->Cancel();
        }
        client_scheduled_tasks_.clear();
        mtx_unlock(&this->task_mtx_);
        delete task;
        delete this;
      });

  // The controller loop is shut down in Controller::DdkRelease, so it is safe to post tasks here.
  this->handler_.CancelFidlBind();
  // The unbind function will run on the controller loop, but it will not be scheduled until all
  // references are dropped. Wait for the handler to actually run.
  sync_completion_wait(handler_.fidl_unbound(), ZX_TIME_INFINITE);
  auto status = task->Post(controller_->loop().dispatcher());
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

zx_status_t ClientProxy::Init(zx::channel server_channel) {
  mtx_init(&task_mtx_, mtx_plain);
  server_channel_ = zx::unowned_channel(server_channel);
  auto seed = static_cast<uint32_t>(zx::clock::get_monotonic().get());
  initial_cookie_ = rand_r(&seed);
  return handler_.Init(std::move(server_channel));
}

ClientProxy::ClientProxy(Controller* controller, bool is_vc, uint32_t client_id)
    : ClientParent(controller->zxdev()),
      controller_(controller),
      is_vc_(is_vc),
      handler_(controller_, this, is_vc_, client_id) {
  mtx_init(&mtx_, mtx_plain);
}

ClientProxy::ClientProxy(Controller* controller, bool is_vc, uint32_t client_id,
                         zx::channel server_channel)
    : ClientParent(nullptr),
      controller_(controller),
      is_vc_(is_vc),
      server_channel_(zx::unowned_channel(server_channel)),
      handler_(controller_, this, is_vc_, client_id, std::move(server_channel)) {
  mtx_init(&mtx_, mtx_plain);
}

ClientProxy::~ClientProxy() {
  mtx_destroy(&mtx_);
  mtx_destroy(&task_mtx_);
}

}  // namespace display

// Banjo C macros and Fidl C++ enum member names collide. Some trickery is required.
namespace {

constexpr auto banjo_CLIENT_USE_PRIMARY = CLIENT_USE_PRIMARY;
constexpr auto banjo_CLIENT_MERGE_BASE = CLIENT_MERGE_BASE;
constexpr auto banjo_CLIENT_MERGE_SRC = CLIENT_MERGE_SRC;
constexpr auto banjo_CLIENT_FRAME_SCALE = CLIENT_FRAME_SCALE;
constexpr auto banjo_CLIENT_SRC_FRAME = CLIENT_SRC_FRAME;
constexpr auto banjo_CLIENT_TRANSFORM = CLIENT_TRANSFORM;
constexpr auto banjo_CLIENT_COLOR_CONVERSION = CLIENT_COLOR_CONVERSION;
constexpr auto banjo_CLIENT_ALPHA = CLIENT_ALPHA;
constexpr auto banjo_CLIENT_GAMMA = CLIENT_GAMMA;
#undef CLIENT_USE_PRIMARY
#undef CLIENT_MERGE_BASE
#undef CLIENT_MERGE_SRC
#undef CLIENT_FRAME_SCALE
#undef CLIENT_SRC_FRAME
#undef CLIENT_TRANSFORM
#undef CLIENT_COLOR_CONVERSION
#undef CLIENT_ALPHA
#undef CLIENT_GAMMA

static_assert((1 << static_cast<int>(fhd::ClientCompositionOpcode::CLIENT_USE_PRIMARY)) ==
                  banjo_CLIENT_USE_PRIMARY,
              "Const mismatch");
static_assert((1 << static_cast<int>(fhd::ClientCompositionOpcode::CLIENT_MERGE_BASE)) ==
                  banjo_CLIENT_MERGE_BASE,
              "Const mismatch");
static_assert((1 << static_cast<int>(fhd::ClientCompositionOpcode::CLIENT_MERGE_SRC)) ==
                  banjo_CLIENT_MERGE_SRC,
              "Const mismatch");
static_assert((1 << static_cast<int>(fhd::ClientCompositionOpcode::CLIENT_FRAME_SCALE)) ==
                  banjo_CLIENT_FRAME_SCALE,
              "Const mismatch");
static_assert((1 << static_cast<int>(fhd::ClientCompositionOpcode::CLIENT_SRC_FRAME)) ==
                  banjo_CLIENT_SRC_FRAME,
              "Const mismatch");
static_assert((1 << static_cast<int>(fhd::ClientCompositionOpcode::CLIENT_TRANSFORM)) ==
                  banjo_CLIENT_TRANSFORM,
              "Const mismatch");
static_assert((1 << static_cast<int>(fhd::ClientCompositionOpcode::CLIENT_COLOR_CONVERSION)) ==
                  banjo_CLIENT_COLOR_CONVERSION,
              "Const mismatch");
static_assert((1 << static_cast<int>(fhd::ClientCompositionOpcode::CLIENT_ALPHA)) ==
                  banjo_CLIENT_ALPHA,
              "Const mismatch");
static_assert((1 << static_cast<int>(fhd::ClientCompositionOpcode::CLIENT_GAMMA)) ==
                  banjo_CLIENT_GAMMA,
              "Const mismatch");

}  // namespace
