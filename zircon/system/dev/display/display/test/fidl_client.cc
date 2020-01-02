#include "fidl_client.h"

#include <ddk/debug.h>

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
  image_config_.type = fhd::typeSimple;
}

uint64_t TestFidlClient::display_id() const { return displays_[0].id_; }

bool TestFidlClient::CreateChannel(zx_handle_t provider, bool is_vc) {
  zx::channel device_server, device_client;
  zx::channel dc_server, dc_client;
  zx_status_t status = zx::channel::create(0, &device_server, &device_client);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not create device channels\n");
    return false;
  }
  status = zx::channel::create(0, &dc_server, &dc_client);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not create controller channels\n");
    return false;
  }
  if (is_vc) {
    auto response = fhd::Provider::Call::OpenVirtconController(
        zx::unowned_channel(provider), std::move(device_server), std::move(dc_server));
    if (!response.ok()) {
      zxlogf(ERROR, "Could not open VC controller, handle=%d error=%d %s\n", provider,
             response.status(), response.error());
      return false;
    }
  } else {
    auto response = fhd::Provider::Call::OpenController(
        zx::unowned_channel(provider), std::move(device_server), std::move(dc_server));
    if (!response.ok()) {
      zxlogf(ERROR, "Could not open controller, handle=%d error=%d %s\n", provider,
             response.status(), response.error());
      return false;
    }
  }
  dc_ = std::make_unique<fhd::Controller::SyncClient>(std::move(dc_client));
  device_handle_.reset(device_client.release());
  return true;
}

bool TestFidlClient::Bind(async_dispatcher_t* dispatcher) {
  while (displays_.is_empty() || !has_ownership_) {
    auto result = dc_->HandleEvents({
        .displays_changed =
            [this](::fidl::VectorView<fhd::Info> added, ::fidl::VectorView<uint64_t> removed) {
              for (size_t i = 0; i < added.count(); i++) {
                displays_.push_back(Display(added[i]));
              }
              return ZX_OK;
            },
        .vsync = [](uint64_t display_id, uint64_t timestamp,
                    ::fidl::VectorView<uint64_t> images) { return ZX_ERR_INVALID_ARGS; },
        .client_ownership_change =
            [this](bool owns) {
              has_ownership_ = owns;
              return ZX_OK;
            },
        .unknown = []() { return ZX_ERR_STOP; },
    });
    if (result != ZX_OK) {
      zxlogf(ERROR, "Got unexpected message %d\n", result);
      return false;
    }
  }

  EXPECT_TRUE(has_ownership_);
  EXPECT_FALSE(displays_.is_empty());
  {
    auto reply = dc_->CreateLayer();
    if (!reply.ok()) {
      zxlogf(ERROR, "Failed to create layer (fidl=%d)\n", reply.status());
      return reply.status();
    } else if (reply->res != ZX_OK) {
      zxlogf(ERROR, "Failed to create layer (res=%d)\n", reply->res);
      return false;
    }
    EXPECT_EQ(dc_->SetLayerPrimaryConfig(reply->layer_id, displays_[0].image_config_).status(),
              ZX_OK);
    layer_id_ = reply->layer_id;
  }
  EXPECT_EQ(ZX_OK, ImportImageWithSysmem(displays_[0].image_config_, &image_id_));
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

  auto result = dc_->HandleEvents({
      .displays_changed = [](::fidl::VectorView<fhd::Info>,
                             ::fidl::VectorView<uint64_t>) { return ZX_OK; },
      .vsync =
          [this](uint64_t, uint64_t, ::fidl::VectorView<uint64_t>) {
            vsync_count_++;
            return ZX_OK;
          },
      .client_ownership_change = [](bool) { return ZX_OK; },
      .unknown = []() { return ZX_ERR_STOP; },
  });

  // Re-arm the wait.
  self->Begin(dispatcher);

  if (result != ZX_OK) {
    zxlogf(ERROR, "Failed to handle events: %d\n", result);
  }
}

zx_status_t TestFidlClient::PresentImage() {
  ZX_ASSERT(layer_id_ != 0);
  uint64_t layers[] = {layer_id_};
  if (auto reply = dc_->SetDisplayLayers(display_id(), {layers, 1}); !reply.ok()) {
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

TestFidlClient::~TestFidlClient() {
  ASSERT_TRUE(dc_ != nullptr);
  if (layer_id_ != 0) {
    dc_->SetDisplayLayers(display_id(), {});
    dc_->DestroyLayer(layer_id_);
  }
  if (image_id_ != 0) {
    dc_->ReleaseImage(image_id_);
  }
  wait_events_.Cancel();
}

static bool channel_is_open(const zx::channel& channel) {
  zx_info_handle_basic_t info;
  return channel.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) == ZX_OK;
}

static bool channel_is_open(const zx::unowned_channel& channel) {
  zx_info_handle_basic_t info;
  return channel->get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) == ZX_OK;
}

static bool channel_is_open(zx_handle_t channel) {
  zx_info_handle_basic_t info;
  return zx_object_get_info(channel, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) ==
         ZX_OK;
}

zx_status_t TestFidlClient::ImportImageWithSysmem(const fhd::ImageConfig& image_config,
                                                  uint64_t* image_id) {
  EXPECT_TRUE(channel_is_open(sysmem_->channel()));
  // Create all the tokens.
  std::unique_ptr<sysmem::BufferCollectionToken::SyncClient> local_token;
  {
    zx::channel client, server;
    if (zx::channel::create(0, &client, &server) != ZX_OK) {
      zxlogf(ERROR, "Failed to create channel for shared collection\n");
      return ZX_ERR_NO_MEMORY;
    }
    auto result = sysmem_->AllocateSharedCollection(std::move(server));
    if (!result.ok()) {
      zxlogf(ERROR, "Failed to allocate shared collection %d\n", result.status());
      return result.status();
    }
    local_token = std::make_unique<sysmem::BufferCollectionToken::SyncClient>(std::move(client));
    EXPECT_NE(ZX_HANDLE_INVALID, local_token->channel().get());
  }
  EXPECT_TRUE(channel_is_open(sysmem_->channel()));
  zx::channel display_token;
  {
    zx::channel server;
    if (zx::channel::create(0, &display_token, &server) != ZX_OK) {
      zxlogf(ERROR, "Failed to duplicate token\n");
      return ZX_ERR_NO_MEMORY;
    }
    EXPECT_TRUE(channel_is_open(local_token->channel()));
    if (auto result = local_token->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(server));
        !result.ok()) {
      zxlogf(ERROR, "Failed to duplicate token %d %s\n", result.status(), result.error());
      return ZX_ERR_NO_MEMORY;
    }
  }

  // Set display buffer constraints.
  static uint64_t display_collection_id = 0;
  display_collection_id++;
  EXPECT_TRUE(channel_is_open(local_token->channel()));
  if (auto result = local_token->Sync(); !result.ok()) {
    zxlogf(ERROR, "Failed to sync token %d %s\n", result.status(), result.error());
    return result.status();
  }
  EXPECT_TRUE(channel_is_open(dc_->channel()));
  if (auto result = dc_->ImportBufferCollection(display_collection_id, std::move(display_token));
      !result.ok() || result->res != ZX_OK) {
    zxlogf(ERROR, "Failed to import buffer collection %lu (fidl=%d, res=%d)\n",
           display_collection_id, result.status(), result->res);
    return result.ok() ? result->res : result.status();
  }

  auto set_constraints_result =
      dc_->SetBufferCollectionConstraints(display_collection_id, image_config);
  if (!set_constraints_result.ok() || set_constraints_result->res != ZX_OK) {
    zxlogf(ERROR, "Setting buffer (%dx%d) collection constraints failed: %s\n", image_config.width,
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
      zxlogf(ERROR, "Failed to bind shared collection\n");
      return ZX_ERR_NO_MEMORY;
    }
    sysmem_collection = std::make_unique<sysmem::BufferCollection::SyncClient>(std::move(client));
  }
  sysmem::BufferCollectionConstraints constraints = {};
  constraints.min_buffer_count = 1;
  constraints.usage.none = sysmem::noneUsage;
  zx_status_t status = sysmem_collection->SetConstraints(true, constraints).status();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Unable to set constraints (%d)\n", status);
    return status;
  }
  // Wait for the buffers to be allocated.
  auto info_result = sysmem_collection->WaitForBuffersAllocated();
  if (!info_result.ok() || info_result->status != ZX_OK) {
    zxlogf(ERROR, "Waiting for buffers failed (fidl=%d res=%d)\n", info_result.status(),
           info_result->status);
    return info_result.ok() ? info_result->status : info_result.status();
  }

  auto& info = info_result->buffer_collection_info;
  if (info.buffer_count < 1) {
    zxlogf(ERROR, "Incorrect buffer collection count %d\n", info.buffer_count);
    return ZX_ERR_NO_MEMORY;
  }

  auto import_result = dc_->ImportImage(image_config, display_collection_id, 0);
  if (!import_result.ok() || import_result->res != ZX_OK) {
    *image_id = fhd::invalidId;
    zxlogf(ERROR, "Importing image failed (fidl=%d, res=%d)\n", import_result.status(),
           import_result->res);
    return import_result.ok() ? import_result->res : import_result.status();
  }
  *image_id = import_result->image_id;

  sysmem_collection->Close();
  return ZX_OK;
}

}  // namespace display
