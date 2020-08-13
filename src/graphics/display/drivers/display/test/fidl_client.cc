// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl_client.h"

#include <lib/async/cpp/task.h>

#include <ddk/debug.h>
#include <fbl/auto_lock.h>

namespace fhd = ::llcpp::fuchsia::hardware::display;
namespace sysmem = ::llcpp::fuchsia::sysmem;

namespace display {

TestFidlClient::Display::Display(const fhd::Info& info) {
  id_ = info.id;

  for (size_t i = 0; i < info.pixel_format.count(); i++) {
    pixel_formats_.push_back(info.pixel_format[i]);
  }
  for (size_t i = 0; i < info.modes.count(); i++) {
    modes_.push_back(info.modes[i]);
  }
  for (size_t i = 0; i < info.cursor_configs.count(); i++) {
    cursors_.push_back(info.cursor_configs[i]);
  }
  manufacturer_name_ = fbl::String(info.manufacturer_name.data());
  monitor_name_ = fbl::String(info.monitor_name.data());
  monitor_serial_ = fbl::String(info.monitor_serial.data());
  image_config_.height = modes_[0].vertical_resolution;
  image_config_.width = modes_[0].horizontal_resolution;
  image_config_.pixel_format = pixel_formats_[0];
  image_config_.type = fhd::TYPE_SIMPLE;
}

uint64_t TestFidlClient::display_id() const { return displays_[0].id_; }

bool TestFidlClient::CreateChannel(zx_handle_t provider, bool is_vc) {
  zx::channel device_server, device_client;
  zx::channel dc_server, dc_client;
  zx_status_t status = zx::channel::create(0, &device_server, &device_client);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not create device channels");
    return false;
  }
  status = zx::channel::create(0, &dc_server, &dc_client);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not create controller channels");
    return false;
  }
  zxlogf(INFO, "Opening controller");
  if (is_vc) {
    auto response = fhd::Provider::Call::OpenVirtconController(
        zx::unowned_channel(provider), std::move(device_server), std::move(dc_server));
    if (!response.ok()) {
      zxlogf(ERROR, "Could not open VC controller, error=%s", response.error());
      return false;
    }
  } else {
    auto response = fhd::Provider::Call::OpenController(
        zx::unowned_channel(provider), std::move(device_server), std::move(dc_server));
    if (!response.ok()) {
      zxlogf(ERROR, "Could not open controller, error=%s", response.error());
      return false;
    }
  }

  fbl::AutoLock lock(mtx());
  dc_ = std::make_unique<fhd::Controller::SyncClient>(std::move(dc_client));
  device_handle_.reset(device_client.release());
  return true;
}

bool TestFidlClient::Bind(async_dispatcher_t* dispatcher) {
  dispatcher_ = dispatcher;
  while (displays_.is_empty() || !has_ownership_) {
    fbl::AutoLock lock(mtx());
    auto result = dc_->HandleEvents({
        .on_displays_changed =
            [this](::fidl::VectorView<fhd::Info> added, ::fidl::VectorView<uint64_t> removed) {
              for (size_t i = 0; i < added.count(); i++) {
                displays_.push_back(Display(added[i]));
              }
              return ZX_OK;
            },
        .on_vsync = [](uint64_t display_id, uint64_t timestamp, ::fidl::VectorView<uint64_t> images,
                       uint64_t cookie) { return ZX_ERR_INVALID_ARGS; },
        .on_client_ownership_change =
            [this](bool owns) {
              has_ownership_ = owns;
              return ZX_OK;
            },
        .unknown = []() { return ZX_ERR_STOP; },
    });
    if (result != ZX_OK) {
      zxlogf(ERROR, "Got unexpected message");
      return false;
    }
  }

  fbl::AutoLock lock(mtx());
  EXPECT_TRUE(has_ownership_);
  EXPECT_FALSE(displays_.is_empty());
  {
    auto reply = dc_->CreateLayer();
    if (!reply.ok()) {
      zxlogf(ERROR, "Failed to create layer (fidl=%d)", reply.status());
      return reply.status();
    } else if (reply->res != ZX_OK) {
      zxlogf(ERROR, "Failed to create layer (res=%d)", reply->res);
      return false;
    }
    EXPECT_EQ(dc_->SetLayerPrimaryConfig(reply->layer_id, displays_[0].image_config_).status(),
              ZX_OK);
    layer_id_ = reply->layer_id;
  }
  EXPECT_EQ(ZX_OK, ImportImageWithSysmemLocked(displays_[0].image_config_, &image_id_));
  wait_events_.set_object(dc_->channel().get());
  wait_events_.set_trigger(ZX_CHANNEL_READABLE);
  EXPECT_OK(wait_events_.Begin(dispatcher));
  return dc_->EnableVsync(true).ok();
}

void TestFidlClient::OnEventMsgAsync(async_dispatcher_t* dispatcher, async::WaitBase* self,
                                     zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    return;
  }

  if (!(signal->observed & ZX_CHANNEL_READABLE)) {
    return;
  }

  fbl::AutoLock lock(mtx());
  auto result = dc_->HandleEvents({
      .on_displays_changed = [](::fidl::VectorView<fhd::Info>,
                                ::fidl::VectorView<uint64_t>) { return ZX_OK; },
      // The FIDL bindings do not know that the caller holds mtx(), so we can't TA_REQ(mtx()) here.
      .on_vsync =
          [this](uint64_t, uint64_t, ::fidl::VectorView<uint64_t>, uint64_t cookie)
              TA_NO_THREAD_SAFETY_ANALYSIS {
                vsync_count_++;
                if (cookie) {
                  cookie_ = cookie;
                }
                return ZX_OK;
              },
      .on_client_ownership_change = [](bool) { return ZX_OK; },
      .unknown = []() { return ZX_ERR_STOP; },
  });

  if (result != ZX_OK) {
    zxlogf(ERROR, "Failed to handle events: %d", result);
    return;
  }

  if (wait_events_.object() == ZX_HANDLE_INVALID) {
    return;
  }
  // Re-arm the wait.
  self->Begin(dispatcher);
}

TestFidlClient::~TestFidlClient() {
  if (dispatcher_) {
    // Cancel must be issued from the dispatcher thread.
    sync_completion_t done;
    auto task = new async::Task();
    task->set_handler(
        [this, task, done_ptr = &done](async_dispatcher_t*, async::Task*, zx_status_t) {
          wait_events_.Cancel();
          wait_events_.set_object(ZX_HANDLE_INVALID);

          sync_completion_signal(done_ptr);
          delete task;
        });
    if (task->Post(dispatcher_) != ZX_OK) {
      delete task;
      wait_events_.Cancel();
      wait_events_.set_object(ZX_HANDLE_INVALID);
    } else {
      while (true) {
        if (sync_completion_wait(&done, ZX_MSEC(10)) == ZX_OK) {
          break;
        }
      }
    }
  }
}

zx_status_t TestFidlClient::PresentImage() {
  fbl::AutoLock l(mtx());
  EXPECT_NE(0, layer_id_);
  EXPECT_NE(0, image_id_);
  uint64_t layers[] = {layer_id_};
  if (auto reply = dc_->SetDisplayLayers(display_id(), {fidl::unowned_ptr(layers), 1});
      !reply.ok()) {
    return reply.status();
  }
  if (auto reply = dc_->SetLayerImage(layer_id_, image_id_, 0, 0); !reply.ok()) {
    return reply.status();
  }
  if (auto reply = dc_->CheckConfig(false); !reply.ok() || reply->res != fhd::ConfigResult::OK) {
    return reply.ok() ? ZX_ERR_INVALID_ARGS : reply.status();
  }
  return dc_->ApplyConfig().status();
}

zx_status_t TestFidlClient::ImportImageWithSysmem(const fhd::ImageConfig& image_config,
                                                  uint64_t* image_id) {
  fbl::AutoLock lock(mtx());
  return ImportImageWithSysmemLocked(image_config, image_id);
}

zx_status_t TestFidlClient::ImportImageWithSysmemLocked(const fhd::ImageConfig& image_config,
                                                        uint64_t* image_id) {
  // Create all the tokens.
  std::unique_ptr<sysmem::BufferCollectionToken::SyncClient> local_token;
  {
    zx::channel client, server;
    if (zx::channel::create(0, &client, &server) != ZX_OK) {
      zxlogf(ERROR, "Failed to create channel for shared collection");
      return ZX_ERR_NO_MEMORY;
    }
    auto result = sysmem_->AllocateSharedCollection(std::move(server));
    if (!result.ok()) {
      zxlogf(ERROR, "Failed to allocate shared collection %d", result.status());
      return result.status();
    }
    local_token = std::make_unique<sysmem::BufferCollectionToken::SyncClient>(std::move(client));
    EXPECT_NE(ZX_HANDLE_INVALID, local_token->channel().get());
  }
  zx::channel display_token;
  {
    zx::channel server;
    if (zx::channel::create(0, &display_token, &server) != ZX_OK) {
      zxlogf(ERROR, "Failed to duplicate token");
      return ZX_ERR_NO_MEMORY;
    }
    if (auto result = local_token->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(server));
        !result.ok()) {
      zxlogf(ERROR, "Failed to duplicate token %d %s", result.status(), result.error());
      return ZX_ERR_NO_MEMORY;
    }
  }

  // Set display buffer constraints.
  static uint64_t display_collection_id = 0;
  display_collection_id++;
  if (auto result = local_token->Sync(); !result.ok()) {
    zxlogf(ERROR, "Failed to sync token %d %s", result.status(), result.error());
    return result.status();
  }
  if (auto result = dc_->ImportBufferCollection(display_collection_id, std::move(display_token));
      !result.ok() || result->res != ZX_OK) {
    zxlogf(ERROR, "Failed to import buffer collection %lu (fidl=%d, res=%d)", display_collection_id,
           result.status(), result->res);
    return result.ok() ? result->res : result.status();
  }

  auto set_constraints_result =
      dc_->SetBufferCollectionConstraints(display_collection_id, image_config);
  if (!set_constraints_result.ok() || set_constraints_result->res != ZX_OK) {
    zxlogf(ERROR, "Setting buffer (%dx%d) collection constraints failed: %s", image_config.width,
           image_config.height, set_constraints_result.error());
    dc_->ReleaseBufferCollection(display_collection_id);
    return set_constraints_result.ok() ? set_constraints_result->res
                                       : set_constraints_result.status();
  }

  // Use the local collection so we can read out the error if allocation
  // fails, and to ensure everything's allocated before trying to import it
  // into another process.
  std::unique_ptr<sysmem::BufferCollection::SyncClient> sysmem_collection;
  {
    zx::channel client, server;
    if (zx::channel::create(0, &client, &server) != ZX_OK ||
        !sysmem_
             ->BindSharedCollection(std::move(*local_token->mutable_channel()), std::move(server))
             .ok()) {
      zxlogf(ERROR, "Failed to bind shared collection");
      return ZX_ERR_NO_MEMORY;
    }
    sysmem_collection = std::make_unique<sysmem::BufferCollection::SyncClient>(std::move(client));
  }
  sysmem::BufferCollectionConstraints constraints = {};
  constraints.min_buffer_count = 1;
  constraints.usage.none = sysmem::noneUsage;
  // We specify min_size_bytes 1 so that something is specifying a minimum size.  More typically the
  // display client would specify ImageFormatConstraints that implies a non-zero min_size_bytes.
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.min_size_bytes = 1;
  constraints.buffer_memory_constraints.ram_domain_supported = true;
  zx_status_t status = sysmem_collection->SetConstraints(true, constraints).status();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Unable to set constraints (%d)", status);
    return status;
  }
  // Wait for the buffers to be allocated.
  auto info_result = sysmem_collection->WaitForBuffersAllocated();
  if (!info_result.ok() || info_result->status != ZX_OK) {
    zxlogf(ERROR, "Waiting for buffers failed (fidl=%d res=%d)", info_result.status(),
           info_result->status);
    return info_result.ok() ? info_result->status : info_result.status();
  }

  auto& info = info_result->buffer_collection_info;
  if (info.buffer_count < 1) {
    zxlogf(ERROR, "Incorrect buffer collection count %d", info.buffer_count);
    return ZX_ERR_NO_MEMORY;
  }

  auto import_result = dc_->ImportImage(image_config, display_collection_id, 0);
  if (!import_result.ok() || import_result->res != ZX_OK) {
    *image_id = fhd::INVALID_DISP_ID;
    zxlogf(ERROR, "Importing image failed (fidl=%d, res=%d)", import_result.status(),
           import_result->res);
    return import_result.ok() ? import_result->res : import_result.status();
  }
  *image_id = import_result->image_id;

  sysmem_collection->Close();
  return ZX_OK;
}

}  // namespace display
