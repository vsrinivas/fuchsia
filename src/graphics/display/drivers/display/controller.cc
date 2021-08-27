// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "controller.h"

#include <fuchsia/hardware/display/capture/c/banjo.h>
#include <fuchsia/hardware/display/capture/cpp/banjo.h>
#include <fuchsia/hardware/display/clamprgb/cpp/banjo.h>
#include <lib/async/cpp/task.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/trace/event.h>
#include <threads.h>
#include <zircon/threads.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include <audio-proto-utils/format-utils.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/string_printf.h>

#include "client.h"
#include "eld.h"
#include "src/graphics/display/drivers/display/display-bind.h"

namespace fidl_display = fuchsia_hardware_display;

namespace {

// Use the same default watchdog timeout as scenic, which may help ensure watchdog logs/errors
// happen close together and can be correlated.
constexpr uint64_t kWatchdogWarningIntervalMs = 15000;
constexpr uint64_t kWatchdogTimeoutMs = 45000;

// vsync delivery is considered to be stalled if at least this amount of time has elapsed since
// vsync was last observed.
constexpr zx::duration kVsyncStallThreshold = zx::sec(10);
constexpr zx::duration kVsyncMonitorInterval = kVsyncStallThreshold / 2;

bool IsKernelFramebufferEnabled() {
  const char* value = getenv("driver.display.enable-kernel-framebuffer");
  if (!value) {
    return false;
  }
  if ((strcmp(value, "0") == 0) || (strcmp(value, "false") == 0) || (strcmp(value, "off") == 0)) {
    return false;
  }
  return true;
}

}  // namespace

namespace display {

void Controller::PopulateDisplayMode(const edid::timing_params_t& params, display_mode_t* mode) {
  mode->pixel_clock_10khz = params.pixel_freq_10khz;
  mode->h_addressable = params.horizontal_addressable;
  mode->h_front_porch = params.horizontal_front_porch;
  mode->h_sync_pulse = params.horizontal_sync_pulse;
  mode->h_blanking = params.horizontal_blanking;
  mode->v_addressable = params.vertical_addressable;
  mode->v_front_porch = params.vertical_front_porch;
  mode->v_sync_pulse = params.vertical_sync_pulse;
  mode->v_blanking = params.vertical_blanking;
  mode->flags = params.flags;

  static_assert(MODE_FLAG_VSYNC_POSITIVE == edid::timing_params::kPositiveVsync, "");
  static_assert(MODE_FLAG_HSYNC_POSITIVE == edid::timing_params::kPositiveHsync, "");
  static_assert(MODE_FLAG_INTERLACED == edid::timing_params::kInterlaced, "");
  static_assert(MODE_FLAG_ALTERNATING_VBLANK == edid::timing_params::kAlternatingVblank, "");
  static_assert(MODE_FLAG_DOUBLE_CLOCKED == edid::timing_params::kDoubleClocked, "");
}

void Controller::PopulateDisplayTimings(const fbl::RefPtr<DisplayInfo>& info) {
  if (!info->edid.has_value()) {
    return;
  }

  // Go through all the display mode timings and record whether or not
  // a basic layer configuration is acceptable.
  layer_t test_layer = {};
  layer_t* test_layers[] = {&test_layer};
  test_layer.cfg.primary.image.pixel_format = info->pixel_formats[0];
  display_config_t test_config;
  const display_config_t* test_configs[] = {&test_config};
  test_config.display_id = info->id;
  test_config.layer_count = 1;
  test_config.layer_list = test_layers;

  for (auto timing = edid::timing_iterator(&info->edid->base); timing.is_valid(); ++timing) {
    uint32_t width = timing->horizontal_addressable;
    uint32_t height = timing->vertical_addressable;
    bool duplicate = false;
    for (auto& existing_timing : info->edid->timings) {
      if (existing_timing.vertical_refresh_e2 == timing->vertical_refresh_e2 &&
          existing_timing.horizontal_addressable == width &&
          existing_timing.vertical_addressable == height) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }
    test_layer.cfg.primary.image.width = width;
    test_layer.cfg.primary.image.height = height;
    test_layer.cfg.primary.src_frame.width = width;
    test_layer.cfg.primary.src_frame.height = height;
    test_layer.cfg.primary.dest_frame.width = width;
    test_layer.cfg.primary.dest_frame.height = height;
    PopulateDisplayMode(*timing, &test_config.mode);

    uint32_t display_cfg_result;
    uint32_t layer_result = 0;
    size_t display_layer_results_count;
    uint32_t* display_layer_results[] = {&layer_result};
    display_cfg_result = dc_.CheckConfiguration(test_configs, 1, display_layer_results,
                                                &display_layer_results_count);
    if (display_cfg_result == CONFIG_DISPLAY_OK) {
      fbl::AllocChecker ac;
      info->edid->timings.push_back(*timing, &ac);
      if (!ac.check()) {
        zxlogf(WARNING, "Edid skip allocation failed");
        break;
      }
    }
  }
}

void Controller::DisplayControllerInterfaceOnDisplaysChanged(
    const added_display_args_t* displays_added, size_t added_count,
    const uint64_t* displays_removed, size_t removed_count,
    added_display_info_t* out_display_info_list, size_t display_info_count,
    size_t* display_info_actual) {
  ZX_DEBUG_ASSERT(!out_display_info_list || added_count == display_info_count);

  std::unique_ptr<fbl::RefPtr<DisplayInfo>[]> added_success;
  std::unique_ptr<uint64_t[]> removed;
  std::unique_ptr<async::Task> task;
  uint32_t added_success_count = 0;

  fbl::AllocChecker ac;
  if (added_count) {
    added_success = std::unique_ptr<fbl::RefPtr<DisplayInfo>[]>(
        new (&ac) fbl::RefPtr<DisplayInfo>[added_count]);
    if (!ac.check()) {
      zxlogf(ERROR, "No memory when processing hotplug");
      return;
    }
  }
  if (removed_count) {
    removed = std::unique_ptr<uint64_t[]>(new (&ac) uint64_t[removed_count]);
    if (!ac.check()) {
      zxlogf(ERROR, "No memory when processing hotplug");
      return;
    }
    memcpy(removed.get(), displays_removed, removed_count * sizeof(uint64_t));
  }
  task = fbl::make_unique_checked<async::Task>(&ac);
  if (!ac.check()) {
    zxlogf(ERROR, "No memory when processing hotplug");
    return;
  }

  fbl::AutoLock lock(mtx());

  for (unsigned i = 0; i < removed_count; i++) {
    auto target = displays_.erase(displays_removed[i]);
    if (target) {
      image_node_t* n;
      while ((n = list_remove_head_type(&target->images, image_node_t, link))) {
        AssertMtxAliasHeld(n->self->mtx());
        n->self->StartRetire();
        n->self->OnRetire();
        n->self.reset();
      }
    } else {
      zxlogf(DEBUG, "Unknown display %ld removed", displays_removed[i]);
    }
  }

  for (unsigned i = 0; i < added_count; i++) {
    fbl::AllocChecker ac;
    auto info_or_status = DisplayInfo::Create(displays_added[i], &i2c_);
    if (info_or_status.is_error()) {
      zxlogf(INFO, "failed to add display %ld: %s",
             displays_added[i].display_id, info_or_status.status_string());
      continue;
    }
    auto info = std::move(info_or_status.value());
    if (info->edid.has_value()) {
      fbl::Array<uint8_t> eld;
      ComputeEld(info->edid->base, eld);
      dc_.SetEld(info->id, eld.get(), eld.size());
    }
    auto* display_info = out_display_info_list ? &out_display_info_list[i] : nullptr;
    if (display_info && info->edid.has_value()) {
      const edid::Edid& edid = info->edid->base;
      display_info->is_hdmi_out = edid.is_hdmi();
      display_info->is_standard_srgb_out = edid.is_standard_rgb();
      display_info->audio_format_count = static_cast<uint32_t>(info->edid->audio.size());

      static_assert(
          sizeof(display_info->monitor_name) == sizeof(edid::Descriptor::Monitor::data) + 1,
          "Possible overflow");
      static_assert(
          sizeof(display_info->monitor_name) == sizeof(edid::Descriptor::Monitor::data) + 1,
          "Possible overflow");
      strcpy(display_info->manufacturer_id, edid.manufacturer_id());
      strcpy(display_info->monitor_name, edid.monitor_name());
      strcpy(display_info->monitor_serial, edid.monitor_serial());
      display_info->manufacturer_name = edid.manufacturer_name();
      display_info->horizontal_size_mm = edid.horizontal_size_mm();
      display_info->vertical_size_mm = edid.vertical_size_mm();
    }

    if (displays_.insert_or_find(info)) {
      added_success[added_success_count++] = std::move(info);
    } else {
      zxlogf(INFO, "Ignoring duplicate display");
    }
  }
  if (display_info_actual)
    *display_info_actual = added_success_count;

  task->set_handler([this, added_ptr = added_success.release(), removed_ptr = removed.release(),
                     added_success_count, removed_count](async_dispatcher_t* dispatcher,
                                                         async::Task* task, zx_status_t status) {
    if (status == ZX_OK) {
      for (unsigned i = 0; i < added_success_count; i++) {
        if (added_ptr[i]->edid.has_value()) {
          PopulateDisplayTimings(added_ptr[i]);
        }
      }
      fbl::AutoLock lock(mtx());

      std::vector<uint64_t> added_ids(added_success_count);
      uint32_t final_added_success_count = 0;
      for (unsigned i = 0; i < added_success_count; i++) {
        // Dropping some add events can result in spurious removes, but
        // those are filtered out in the clients.
        if (!added_ptr[i]->edid.has_value() || !added_ptr[i]->edid->timings.is_empty()) {
          added_ids[final_added_success_count++] = added_ptr[i]->id;
          added_ptr[i]->init_done = true;
          added_ptr[i]->InitializeInspect(&root_);
        } else {
          zxlogf(WARNING, "Ignoring display with no compatible edid timings");
        }
      }

      if (vc_client_ && vc_ready_) {
        vc_client_->OnDisplaysChanged(added_ids.data(), final_added_success_count, removed_ptr,
                                      removed_count);
      }
      if (primary_client_ && primary_ready_) {
        primary_client_->OnDisplaysChanged(added_ids.data(), final_added_success_count, removed_ptr,
                                           removed_count);
      }

    } else {
      zxlogf(ERROR, "Failed to dispatch display change task %d", status);
    }

    delete[] added_ptr;
    delete[] removed_ptr;
    delete task;
  });
  task.release()->Post(loop_.dispatcher());
}

void Controller::DisplayCaptureInterfaceOnCaptureComplete() {
  std::unique_ptr<async::Task> task = std::make_unique<async::Task>();
  fbl::AutoLock lock(mtx());
  task->set_handler([this](async_dispatcher_t* dispatcher, async::Task* task, zx_status_t status) {
    if (status == ZX_OK) {
      // Free an image that was previously used by the hardware.
      if (pending_capture_image_release_ != 0) {
        ReleaseCaptureImage(pending_capture_image_release_);
        pending_capture_image_release_ = 0;
      }
      fbl::AutoLock lock(mtx());
      if (vc_client_ && vc_ready_) {
        vc_client_->OnCaptureComplete();
      }
      if (primary_client_ && primary_ready_) {
        primary_client_->OnCaptureComplete();
      }
    } else {
      zxlogf(ERROR, "Failed to dispatch capture complete task %d", status);
    }
    delete task;
  });
  task.release()->Post(loop_.dispatcher());
}

void Controller::DisplayControllerInterfaceOnDisplayVsync(uint64_t display_id, zx_time_t timestamp,
                                                          const uint64_t* handles,
                                                          size_t handle_count) {
  // Emit an event called "VSYNC", which is by convention the event
  // that Trace Viewer looks for in its "Highlight VSync" feature.
  TRACE_INSTANT("gfx", "VSYNC", TRACE_SCOPE_THREAD, "display_id", display_id);
  TRACE_DURATION("gfx", "Display::Controller::OnDisplayVsync", "display_id", display_id);

  last_vsync_ns_property_.Set(timestamp);
  last_vsync_interval_ns_property_.Set(timestamp - last_vsync_timestamp_.load().get());
  last_vsync_timestamp_ = zx::time(timestamp);
  vsync_stalled_ = false;

  fbl::AutoLock lock(mtx());
  size_t found_handles = 0;
  DisplayInfo* info = nullptr;
  for (auto& display_config : displays_) {
    if (display_config.id == display_id) {
      info = &display_config;
      break;
    }
  }

  if (!info) {
    zxlogf(ERROR, "No such display %lu", display_id);
    return;
  }

  // See ::ApplyConfig for more explanation of how vsync image tracking works.
  //
  // If there's a pending layer change, don't process any present/retire actions
  // until the change is complete.
  if (info->pending_layer_change) {
    bool done;
    if (handle_count != info->vsync_layer_count) {
      // There's an unexpected number of layers, so wait until the next vsync.
      done = false;
    } else if (list_is_empty(&info->images)) {
      // If the images list is empty, then we can't have any pending layers and
      // the change is done when there are no handles being displayed.
      ZX_ASSERT_MSG(info->vsync_layer_count == 0, "vsync_layer_count = %d",
                    info->vsync_layer_count);
      done = handle_count == 0;
    } else {
      // Otherwise the change is done when the last handle_count==info->layer_count
      // images match the handles in the correct order.
      auto node = list_peek_tail_type(&info->images, image_node_t, link);
      ssize_t handle_idx = handle_count - 1;
      while (handle_idx >= 0 && node != nullptr) {
        if (handles[handle_idx] != node->self->info().handle) {
          break;
        }
        node = list_prev_type(&info->images, &node->link, image_node_t, link);
        handle_idx--;
      }
      done = handle_idx == -1;
    }

    if (done) {
      info->pending_layer_change = false;
      info->switching_client = false;

      if (active_client_ && info->delayed_apply) {
        active_client_->ReapplyConfig();
      }
    }
  }

  if (!info->pending_layer_change) {
    // Since we know there are no pending layer changes, we know that every layer (i.e z_index)
    // has an image. So every image either matches a handle (in which case it's being
    // displayed), is older than its layer's image (i.e. in front of in the queue) and can be
    // retired, or is newer than its layer's image (i.e. behind in the queue) and has yet to be
    // presented.

    std::vector<uint32_t> z_indices(handle_count);
    for (unsigned i = 0; i < handle_count; i++) {
      z_indices[i] = UINT32_MAX;
    }
    image_node_t* cur;
    image_node_t* tmp;
    list_for_every_entry_safe (&info->images, cur, tmp, image_node_t, link) {
      bool z_already_matched = false;
      for (unsigned i = 0; i < handle_count; i++) {
        if (handles[i] == cur->self->info().handle) {
          z_indices[i] = cur->self->z_index();
          z_already_matched = true;
          break;
        } else if (z_indices[i] == cur->self->z_index()) {
          z_already_matched = true;
          break;
        }
      }

      // Retire any images for which we don't already have a z-match, since
      // those are older than whatever is currently in their layer.
      if (!z_already_matched) {
        list_delete(&cur->link);
        AssertMtxAliasHeld(cur->self->mtx());
        cur->self->OnRetire();
        // Older images may not be presented. Ending their flows here
        // ensures the sanity of traces.
        //
        // NOTE: If changing this flow name or ID, please also do so in the
        // corresponding FLOW_BEGIN in display_swapchain.cc.
        TRACE_FLOW_END("gfx", "present_image", cur->self->id);
        cur->self.reset();
      }
    }
  }

  std::vector<uint64_t> primary_images, virtcon_images;
  image_node_t* cur;
  list_for_every_entry (&info->images, cur, image_node_t, link) {
    for (unsigned i = 0; i < handle_count; i++) {
      if (handles[i] == cur->self->info().handle) {
        // End of the flow for the image going to be presented.
        //
        // NOTE: If changing this flow name or ID, please also do so in the
        // corresponding FLOW_BEGIN in display_swapchain.cc.
        TRACE_FLOW_END("gfx", "present_image", cur->self->id);
        bool is_virtcon_image = vc_client_ && cur->self->client_id() == vc_client_->id();
        bool is_primary_image = primary_client_ && cur->self->client_id() == primary_client_->id();
        if (is_virtcon_image) {
          virtcon_images.push_back(cur->self->id);
        }
        if (is_primary_image) {
          primary_images.push_back(cur->self->id);
        }
        if (is_virtcon_image || is_primary_image) {
          found_handles++;
          break;
        }
      }
    }
  }

  if (found_handles != handle_count) {
    zxlogf(TRACE,
           "OnDisplayVsync with %lu unmatched images (found_handles = %lu, handle_count = %lu)\n",
           handle_count - found_handles, found_handles, handle_count);
    return;
  }

  if (vc_applied_ && vc_client_) {
    vc_client_->OnDisplayVsync(display_id, timestamp, virtcon_images.data(), virtcon_images.size());
  } else if (!vc_applied_ && primary_client_) {
    // A previous client applied a config and then disconnected before the vsync. Don't send garbage
    // image IDs to the new primary client.
    if (primary_client_->id() != applied_client_id_) {
      zxlogf(DEBUG,
             "Dropping vsync. This was meant for client[%d], "
             "but client[%d] is currently active.\n",
             applied_client_id_, primary_client_->id());
    } else {
      primary_client_->OnDisplayVsync(display_id, timestamp, primary_images.data(),
                                      primary_images.size());
    }
  }
}

zx_status_t Controller::DisplayControllerInterfaceGetAudioFormat(
    uint64_t display_id, uint32_t fmt_idx, audio_types_audio_stream_format_range_t* fmt_out) {
  fbl::AutoLock lock(mtx());
  auto display = displays_.find(display_id);
  if (!display.IsValid()) {
    return ZX_ERR_NOT_FOUND;
  }

  if (!display->edid.has_value()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (fmt_idx > display->edid->audio.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  *fmt_out = display->edid->audio[fmt_idx];
  return ZX_OK;
}

void Controller::ApplyConfig(DisplayConfig* configs[], int32_t count, bool is_vc,
                             uint32_t client_stamp, uint32_t client_id) {
  zx_time_t timestamp = zx_clock_get_monotonic();
  last_valid_apply_config_timestamp_ns_property_.Set(timestamp);
  last_valid_apply_config_interval_ns_property_.Set(timestamp - last_valid_apply_config_timestamp_);
  last_valid_apply_config_timestamp_ = timestamp;

  fbl::Array<const display_config_t*> display_configs(new const display_config_t*[count], count);
  uint32_t display_count = 0;
  {
    fbl::AutoLock lock(mtx());
    bool switching_client = (is_vc != vc_applied_ || client_id != applied_client_id_);
    // The fact that there could already be a vsync waiting to be handled when a config
    // is applied means that a vsync with no handle for a layer could be interpreted as either
    // nothing in the layer has been presented or everything in the layer can be retired. To
    // prevent that ambiguity, we don't allow a layer to be disabled until an image from
    // it has been displayed.
    //
    // Since layers can be moved between displays but the implementation only supports
    // tracking the image in one display's queue, we need to ensure that the old display is
    // done with a migrated image before the new display is done with it. This means
    // that the new display can't flip until the configuration change is done. However, we
    // don't want to completely prohibit flips, as that would add latency if the layer's new
    // image is being waited for when the configuration is applied.
    //
    // To handle both of these cases, we force all layer changes to complete before the client
    // can apply a new configuration. We allow the client to apply a more complete version of
    // the configuration, although Client::HandleApplyConfig won't migrate a layer's current
    // image if there is also a pending image.
    if (switching_client || applied_stamp_ != client_stamp) {
      for (int i = 0; i < count; i++) {
        auto* config = configs[i];
        auto display = displays_.find(config->id);
        if (!display.IsValid()) {
          continue;
        }

        if (display->pending_layer_change) {
          display->delayed_apply = true;
          return;
        }
      }
    }

    for (int i = 0; i < count; i++) {
      auto* config = configs[i];
      auto display = displays_.find(config->id);
      if (!display.IsValid()) {
        continue;
      }

      display->switching_client = switching_client;
      display->pending_layer_change = config->apply_layer_change();
      display->vsync_layer_count = config->vsync_layer_count();
      display->delayed_apply = false;

      if (display->vsync_layer_count == 0) {
        continue;
      }

      display_configs[display_count++] = config->current_config();

      for (auto& layer_node : config->get_current_layers()) {
        Layer* layer = layer_node.layer;
        fbl::RefPtr<Image> image = layer->current_image();

        if (layer->is_skipped() || !image) {
          continue;
        }

        // Set the image z index so vsync knows what layer the image is in
        AssertMtxAliasHeld(image->mtx());
        image->set_z_index(layer->z_order());
        image->StartPresent();

        // It's possible that the image's layer was moved between displays. The logic around
        // pending_layer_change guarantees that the old display will be done with the image
        // before the new display is, so deleting it from the old list is fine.
        //
        // Even if we're on the same display, the entry needs to be moved to the end of the
        // list to ensure that the last config->current.layer_count elements in the queue
        // are the current images.
        if (list_in_list(&image->node.link)) {
          list_delete(&image->node.link);
        } else {
          image->node.self = image;
        }
        list_add_tail(&display->images, &image->node.link);
      }
      ZX_ASSERT(display->vsync_layer_count == 0 || !list_is_empty(&display->images));
    }

    vc_applied_ = is_vc;
    applied_stamp_ = client_stamp;
    applied_client_id_ = client_id;
    if (switching_client) {
      active_client_->ReapplySpecialConfigs();
    }
  }
  dc_.ApplyConfiguration(display_configs.get(), display_count);
}

void Controller::ReleaseImage(Image* image) { dc_.ReleaseImage(&image->info()); }

void Controller::ReleaseCaptureImage(uint64_t handle) {
  if (dc_capture_.is_valid() && handle != 0) {
    if (dc_capture_.ReleaseCapture(handle) == ZX_ERR_SHOULD_WAIT) {
      ZX_DEBUG_ASSERT_MSG(pending_capture_image_release_ == 0,
                          "multiple pending releases for capture images");
      // Delay the image release until the hardware is done.
      pending_capture_image_release_ = handle;
    }
  }
}

void Controller::SetVcMode(uint8_t vc_mode) {
  fbl::AutoLock lock(mtx());
  vc_mode_ = static_cast<fidl_display::wire::VirtconMode>(vc_mode);
  HandleClientOwnershipChanges();
}

void Controller::HandleClientOwnershipChanges() {
  ClientProxy* new_active;
  if (vc_mode_ == fidl_display::wire::VirtconMode::kForced ||
      (vc_mode_ == fidl_display::wire::VirtconMode::kFallback && primary_client_ == nullptr)) {
    new_active = vc_client_;
  } else {
    new_active = primary_client_;
  }

  if (new_active != active_client_) {
    if (active_client_) {
      active_client_->SetOwnership(false);
    }
    if (new_active) {
      new_active->SetOwnership(true);
    }
    active_client_ = new_active;
  }
}

void Controller::OnClientDead(ClientProxy* client) {
  zxlogf(DEBUG, "Client %d dead", client->id());
  fbl::AutoLock lock(mtx());
  if (unbinding_) {
    return;
  }
  if (client == vc_client_) {
    vc_client_ = nullptr;
    vc_mode_ = fidl_display::wire::VirtconMode::kInactive;
  } else if (client == primary_client_) {
    primary_client_ = nullptr;
  } else {
    ZX_DEBUG_ASSERT_MSG(false, "Dead client is neither vc nor primary\n");
  }
  HandleClientOwnershipChanges();
}

bool Controller::GetPanelConfig(uint64_t display_id,
                                const fbl::Vector<edid::timing_params_t>** timings,
                                const display_params_t** params) {
  ZX_DEBUG_ASSERT(mtx_trylock(&mtx_) == thrd_busy);
  if (unbinding_) {
    return false;
  }
  for (auto& display : displays_) {
    if (display.id == display_id) {
      if (display.edid.has_value()) {
        *timings = &display.edid->timings;
        *params = nullptr;
      } else {
        *params = &display.params;
        *timings = nullptr;
      }
      return true;
    }
  }
  return false;
}

#define GET_DISPLAY_INFO(FN_NAME, FIELD, TYPE)                                \
  bool Controller::FN_NAME(uint64_t display_id, fbl::Array<TYPE>* data_out) { \
    ZX_DEBUG_ASSERT(mtx_trylock(&mtx_) == thrd_busy);                         \
    for (auto& display : displays_) {                                         \
      if (display.id == display_id) {                                         \
        fbl::AllocChecker ac;                                                 \
        size_t size = display.FIELD.size();                                   \
        *data_out = fbl::Array<TYPE>(new (&ac) TYPE[size], size);             \
        if (!ac.check()) {                                                    \
          return false;                                                       \
        }                                                                     \
        memcpy(data_out->data(), display.FIELD.data(), sizeof(TYPE) * size);  \
        return true;                                                          \
      }                                                                       \
    }                                                                         \
    return false;                                                             \
  }

GET_DISPLAY_INFO(GetCursorInfo, cursor_infos, cursor_info_t)
GET_DISPLAY_INFO(GetSupportedPixelFormats, pixel_formats, zx_pixel_format_t)

bool Controller::GetDisplayIdentifiers(uint64_t display_id, const char** manufacturer_name,
                                       const char** monitor_name, const char** monitor_serial) {
  ZX_DEBUG_ASSERT(mtx_trylock(&mtx_) == thrd_busy);
  for (auto& display : displays_) {
    if (display.id == display_id) {
      display.GetIdentifiers(manufacturer_name, monitor_name, monitor_serial);
      return true;
    }
  }
  return false;
}

bool Controller::GetDisplayPhysicalDimensions(uint64_t display_id, uint32_t* horizontal_size_mm,
                                              uint32_t* vertical_size_mm) {
  ZX_DEBUG_ASSERT(mtx_trylock(&mtx_) == thrd_busy);
  for (auto& display : displays_) {
    if (display.id == display_id) {
      display.GetPhysicalDimensions(horizontal_size_mm, vertical_size_mm);
      return true;
    }
  }
  return false;
}

zx_status_t Controller::DdkOpen(zx_device_t** dev_out, uint32_t flags) { return ZX_OK; }

static void PrintChannelKoids(bool is_vc, const zx::channel& channel) {
  zx_info_handle_basic_t info{};
  size_t actual, avail;
  zx_status_t status = channel.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), &actual, &avail);
  if (status != ZX_OK || info.type != ZX_OBJ_TYPE_CHANNEL) {
    zxlogf(DEBUG, "Could not get koids for handle(type=%d): %d", info.type, status);
    return;
  }
  ZX_DEBUG_ASSERT(actual == avail);
  zxlogf(INFO, "%s client connecting on channel (c=0x%lx, s=0x%lx)", is_vc ? "vc" : "dc",
         info.related_koid, info.koid);
}

zx_status_t Controller::CreateClient(bool is_vc, zx::channel device_channel,
                                     zx::channel client_channel,
                                     fit::function<void()> on_client_dead) {
  PrintChannelKoids(is_vc, client_channel);
  fbl::AllocChecker ac;
  std::unique_ptr<async::Task> task = fbl::make_unique_checked<async::Task>(&ac);
  if (!ac.check()) {
    zxlogf(DEBUG, "Failed to alloc client task");
    return ZX_ERR_NO_MEMORY;
  }

  fbl::AutoLock lock(mtx());
  if (unbinding_) {
    zxlogf(DEBUG, "Client connected during unbind");
    return ZX_ERR_UNAVAILABLE;
  }

  if ((is_vc && vc_client_) || (!is_vc && primary_client_)) {
    zxlogf(DEBUG, "Already bound");
    return ZX_ERR_ALREADY_BOUND;
  }

  // Kernel framebuffer currently prevent non-linear formats and result
  // in a significant performance cost each time a new config is applied.
  // We limit usage to virtcon mode until these problems have been
  // resolved.
  bool use_kernel_framebuffer = is_vc && kernel_framebuffer_enabled_;

  auto client = fbl::make_unique_checked<ClientProxy>(&ac, this, is_vc, use_kernel_framebuffer,
                                                      next_client_id_++, std::move(on_client_dead));
  if (!ac.check()) {
    zxlogf(DEBUG, "Failed to alloc client");
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = client->Init(&root_, std::move(client_channel));
  if (status != ZX_OK) {
    zxlogf(DEBUG, "Failed to init client %d", status);
    return status;
  }

  status = client->DdkAdd(ddk::DeviceAddArgs(is_vc ? "dc-vc" : "dc")
                              .set_flags(DEVICE_ADD_INSTANCE)
                              .set_client_remote(std::move(device_channel)));

  if (status != ZX_OK) {
    zxlogf(DEBUG, "Failed to add client %d", status);
    return status;
  }

  ClientProxy* client_ptr = client.release();

  zxlogf(DEBUG, "New %s client [%d] connected.", is_vc ? "dc-vc" : "dc", client_ptr->id());

  if (is_vc) {
    vc_client_ = client_ptr;
    vc_ready_ = false;
  } else {
    primary_client_ = client_ptr;
    primary_ready_ = false;
  }
  HandleClientOwnershipChanges();

  task->set_handler(
      [this, client_ptr](async_dispatcher_t* dispatcher, async::Task* task, zx_status_t status) {
        if (status == ZX_OK) {
          fbl::AutoLock lock(mtx());
          if (unbinding_) {
            return;
          }
          if (client_ptr == vc_client_ || client_ptr == primary_client_) {
            // Add all existing displays to the client
            if (displays_.size() > 0) {
              uint64_t current_displays[displays_.size()];
              int idx = 0;
              for (const DisplayInfo& display : displays_) {
                if (display.init_done) {
                  current_displays[idx++] = display.id;
                }
              }
              client_ptr->OnDisplaysChanged(current_displays, idx, nullptr, 0);
            }

            if (vc_client_ == client_ptr) {
              vc_ready_ = true;
            } else {
              primary_ready_ = true;
            }
          }
        }
        delete task;
      });

  return task.release()->Post(loop_.dispatcher());
}

void Controller::OpenVirtconController(OpenVirtconControllerRequestView request,
                                       OpenVirtconControllerCompleter::Sync& _completer) {
  _completer.Reply(
      CreateClient(/*is_vc=*/true, std::move(request->device), request->controller.TakeChannel()));
}

void Controller::OpenController(OpenControllerRequestView request,
                                OpenControllerCompleter::Sync& _completer) {
  _completer.Reply(
      CreateClient(/*is_vc=*/false, std::move(request->device), request->controller.TakeChannel()));
}

void Controller::OnVsyncMonitor() {
  if (vsync_stalled_) {
    return;
  }

  if ((zx::clock::get_monotonic() - last_vsync_timestamp_.load()) > kVsyncStallThreshold) {
    vsync_stalled_ = true;
    vsync_stalls_detected_.Add(1);
  }

  zx_status_t status = vsync_monitor_.PostDelayed(loop_.dispatcher(), kVsyncMonitorInterval);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to schedule vsync monitor: %s", zx_status_get_string(status));
  }
}

zx_status_t Controller::Bind(std::unique_ptr<display::Controller>* device_ptr) {
  ZX_DEBUG_ASSERT_MSG(device_ptr && device_ptr->get() == this, "Wrong controller passed to Bind()");

  zx_status_t status;
  dc_ = ddk::DisplayControllerImplProtocolClient(parent_);
  if (!dc_.is_valid()) {
    ZX_DEBUG_ASSERT_MSG(false, "Display controller bind mismatch");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // optional display controller capture protocol client
  dc_capture_ = ddk::DisplayCaptureImplProtocolClient(parent_);
  if (!dc_capture_.is_valid()) {
    zxlogf(WARNING, "Display Capture not supported by this platform");
  }

  // optional display controller clamp rgb protocol client
  dc_clamp_rgb_ = ddk::DisplayClampRgbImplProtocolClient(parent_);

  i2c_ = ddk::I2cImplProtocolClient(parent_);

  status = loop_.StartThread("display-client-loop", &loop_thread_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to start loop %d", status);
    return status;
  }

  if ((status = DdkAdd(
           ddk::DeviceAddArgs("display-controller").set_inspect_vmo(inspector_.DuplicateVmo()))) !=
      ZX_OK) {
    zxlogf(ERROR, "Failed to add display core device %d", status);
    return status;
  }

  // Set the display controller looper thread to use a deadline profile.
  // TODO(fxbug.dev/40858): Migrate to the role-based API when available, instead of hard
  // coding parameters.
  {
    const zx_duration_t capacity = ZX_USEC(500);
    const zx_duration_t deadline = ZX_MSEC(8);
    const zx_duration_t period = deadline;

    zx_handle_t profile = ZX_HANDLE_INVALID;
    if ((status = device_get_deadline_profile(this->zxdev(), capacity, deadline, period,
                                              "dev/display/controller", &profile)) != ZX_OK) {
      zxlogf(ERROR, "Failed to get deadline profile %d", status);
    } else {
      zx_handle_t thread_handle = thrd_get_zx_handle(loop_thread_);
      status = zx_object_set_profile(thread_handle, profile, 0);
      if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to set deadline profile %d", status);
      }
      zx_handle_close(profile);
    }
  }

  __UNUSED auto ptr = device_ptr->release();

  dc_.SetDisplayControllerInterface(this, &display_controller_interface_protocol_ops_);
  if (dc_capture_.is_valid()) {
    dc_capture_.SetDisplayCaptureInterface(this, &display_capture_interface_protocol_ops_);
  }

  status = vsync_monitor_.PostDelayed(loop_.dispatcher(), kVsyncMonitorInterval);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to schedule vsync monitor: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

void Controller::DdkUnbind(ddk::UnbindTxn txn) {
  zxlogf(INFO, "Controller::DdkUnbind");
  fbl::AutoLock lock(mtx());
  unbinding_ = true;
  txn.Reply();
}

void Controller::DdkRelease() {
  vsync_monitor_.Cancel();
  // Clients may have active work holding mtx_ in loop_.dispatcher(), so shut it down without mtx_
  loop_.Shutdown();
  // Set an empty config so that the display driver releases resources.
  const display_config_t* configs;
  dc_.ApplyConfiguration(&configs, 0);
  delete this;
}

Controller::Controller(zx_device_t* parent)
    : ControllerParent(parent),
      kernel_framebuffer_enabled_(IsKernelFramebufferEnabled()),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      watchdog_("display-client-loop", kWatchdogWarningIntervalMs, kWatchdogTimeoutMs,
                loop_.dispatcher()) {
  mtx_init(&mtx_, mtx_plain);
  root_ = inspector_.GetRoot().CreateChild("display");
  last_vsync_ns_property_ = root_.CreateUint("last_vsync_timestamp_ns", 0);
  last_vsync_interval_ns_property_ = root_.CreateUint("last_vsync_interval_ns", 0);
  last_valid_apply_config_timestamp_ns_property_ =
      root_.CreateUint("last_valid_apply_config_timestamp_ns", 0);
  last_valid_apply_config_interval_ns_property_ =
      root_.CreateUint("last_valid_apply_config_interval_ns", 0);
  vsync_stalls_detected_ = root_.CreateUint("vsync_stalls", 0);
}

Controller::~Controller() { zxlogf(INFO, "Controller::~Controller"); }

size_t Controller::TEST_imported_images_count() const {
  fbl::AutoLock lock(mtx());
  size_t vc_images = vc_client_ ? vc_client_->TEST_imported_images_count() : 0;
  size_t primary_images = primary_client_ ? primary_client_->TEST_imported_images_count() : 0;
  size_t display_images = 0;
  for (const auto& display : displays_) {
    image_node_t* cur;
    image_node_t* tmp;
    list_for_every_entry_safe (&display.images, cur, tmp, image_node_t, link) { ++display_images; }
  }
  return vc_images + primary_images + display_images;
}

// ControllerInstance methods

}  // namespace display

static zx_status_t display_controller_bind(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  std::unique_ptr<display::Controller> core(new (&ac) display::Controller(parent));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  return core->Bind(&core);
}

static constexpr zx_driver_ops_t display_controller_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = display_controller_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER(display_controller, display_controller_ops, "zircon", "0.1");
