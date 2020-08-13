// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/factory/streamer.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/global.h>

namespace camera {

Streamer::Streamer() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

Streamer::~Streamer() {
  loop_.Quit();
  loop_.JoinThreads();
}

fit::result<std::unique_ptr<Streamer>, zx_status_t> Streamer::Create(
    fuchsia::sysmem::AllocatorHandle allocator, fuchsia::camera3::DeviceWatcherHandle watcher,
    fit::closure stop_callback) {
  auto streamer = std::make_unique<Streamer>();

  streamer->stop_callback_ = std::move(stop_callback);

  streamer->allocator_.set_error_handler(
      [streamer = streamer.get()](zx_status_t status) { streamer->loop_.Quit(); });
  zx_status_t status =
      streamer->allocator_.Bind(std::move(allocator), streamer->loop_.dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  streamer->watcher_.set_error_handler(
      [streamer = streamer.get()](zx_status_t status) { streamer->stop_callback_(); });
  status = streamer->watcher_.Bind(std::move(watcher), streamer->loop_.dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  streamer->watcher_->WatchDevices(
      fit::bind_member(streamer.get(), &Streamer::WatchDevicesCallback));

  status = streamer->Start();
  if (status != ZX_OK) {
    return fit::error(status);
  }

  return fit::ok(std::move(streamer));
}

zx_status_t Streamer::Start() {
  zx_status_t status = ZX_OK;

  status = loop_.StartThread("Streamer Loop");
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
  }
  return status;
}

void Streamer::WatchDevicesCallback(std::vector<fuchsia::camera3::WatchDevicesEvent> events) {
  for (auto& event : events) {
    if (event.is_added()) {
      // Connect to device.
      // TODO(48506) Properly detect expected device id.
      watcher_->ConnectToDevice(event.added(), device_.NewRequest(loop_.dispatcher()));

      // Fetch camera configurations
      device_->GetConfigurations(
          [this](std::vector<fuchsia::camera3::Configuration> configurations) {
            config_count_ = 0;
            connected_config_index_ = 0;
            configurations_ = std::move(configurations);
            config_count_ = configurations.size();
            FX_LOGS(INFO) << "configurations available: " << config_count_;
            // Once we have the known camera configurations, default to the first configuration
            // index. This is automatically chosen in the driver, so we do not need to ask for it.
            // The callback for WatchCurrentConfiguration() will connect to all streams.
            device_->WatchCurrentConfiguration(
                fit::bind_member(this, &Streamer::WatchCurrentConfigurationCallback));
          });
    }
  }

  // Hanging get.
  watcher_->WatchDevices(fit::bind_member(this, &Streamer::WatchDevicesCallback));
}

void Streamer::WatchCurrentConfigurationCallback(uint32_t config_index) {
  // Start connecting to all streams.
  ConnectToAllStreams();

  // Be ready for configuration changes.
  device_->WatchCurrentConfiguration(
      fit::bind_member(this, &Streamer::WatchCurrentConfigurationCallback));
}

void Streamer::ConnectToAllStreams() {
  connected_stream_count_ = 0;
  ConnectToStream(connected_config_index_, connected_stream_count_);
}

void Streamer::ConnectToStream(uint32_t config_index, uint32_t stream_index) {
  FX_LOGS(INFO) << "Connecting to c" << config_index << "s" << stream_index << " of "
                << configurations_[config_index].streams.size();
  ZX_ASSERT(configurations_.size() > config_index);
  ZX_ASSERT(configurations_[config_index].streams.size() > stream_index);

  // Connect to specific stream
  StreamInfo new_stream_info;
  stream_infos_.emplace(stream_index, std::move(new_stream_info));
  auto& stream_info = stream_infos_[stream_index];

  auto& stream = stream_info.stream;
  auto stream_request = stream.NewRequest(loop_.dispatcher());

  // Allocate buffer collection
  fuchsia::sysmem::BufferCollectionTokenHandle token_orig;
  allocator_->AllocateSharedCollection(token_orig.NewRequest());
  stream->SetBufferCollection(std::move(token_orig));
  stream->WatchBufferCollection(
      [this, config_index, stream_index](fuchsia::sysmem::BufferCollectionTokenHandle token_back) {
        WatchBufferCollectionCallback(config_index, stream_index, std::move(token_back));
      });
  device_->ConnectToStream(stream_index, std::move(stream_request));
  stream.set_error_handler(
      [this, stream_index](zx_status_t status) { DisconnectStream(stream_index); });
}

void Streamer::WatchBufferCollectionCallback(
    uint32_t config_index, uint32_t stream_index,
    fuchsia::sysmem::BufferCollectionTokenHandle token_back) {
  auto& stream_info = stream_infos_[stream_index];

  allocator_->BindSharedCollection(std::move(token_back),
                                   stream_info.collection.NewRequest(loop_.dispatcher()));

  // Set minimal constraints then wait for buffer allocation.
  stream_info.collection->SetConstraints(
      true, {.usage{.cpu = fuchsia::sysmem::cpuUsageRead},
             .min_buffer_count_for_camping = 2,
             .has_buffer_memory_constraints = true,
             .buffer_memory_constraints{.ram_domain_supported = true}});
  stream_info.collection->WaitForBuffersAllocated(
      [this, stream_index](zx_status_t status,
                           fuchsia::sysmem::BufferCollectionInfo_2 buffers) mutable {
        WaitForBuffersAllocatedCallback(stream_index, status, std::move(buffers));
      });
}

void Streamer::WaitForBuffersAllocatedCallback(uint32_t stream_index, zx_status_t status,
                                               fuchsia::sysmem::BufferCollectionInfo_2 buffers) {
  auto& stream_info = stream_infos_[stream_index];
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to allocate buffers.";
    return;
  }
  stream_info.collection_info = std::move(buffers);

  stream_info.collection->Close();

  connected_stream_count_++;

  // BEGIN: Daisy-chain work around for fxb/42241
  const uint32_t stream_count = configurations_[connected_config_index_].streams.size();
  if (connected_stream_count_ < stream_count) {
    ConnectToStream(connected_config_index_, connected_stream_count_);
  }
  // END: Daisy-chain work around for fxb/42241

  auto& stream = stream_infos_[stream_index].stream;
  stream->GetNextFrame([this, stream_index](fuchsia::camera3::FrameInfo frame_info) {
    OnNextFrame(stream_index, std::move(frame_info));
  });
}

void Streamer::OnNextFrame(uint32_t stream_index, fuchsia::camera3::FrameInfo frame_info) {
  frame_count_++;
  auto& stream_info = stream_infos_[stream_index];

  if (capture_ && capture_->stream_ == stream_index) {
    // capture a frame to memory
    capture_->properties_ = configurations_[connected_config_index_].streams[stream_index];
    if (capture_->want_image_) {
      auto size = stream_info.collection_info.settings.buffer_settings.size_bytes;
      auto& vmo = stream_info.collection_info.buffers[frame_info.buffer_index].vmo;
      ZX_ASSERT(vmo.is_valid());
      capture_->image_->resize(size);
      vmo.read(capture_->image_->data(), 0, size);
    }
    capture_->callback_(ZX_OK, std::move(capture_));
    capture_ = nullptr;  // needed?
  }
  frame_info.release_fence.reset();

  auto& stream = stream_info.stream;
  stream->GetNextFrame([this, stream_index](fuchsia::camera3::FrameInfo frame_info) {
    OnNextFrame(stream_index, std::move(frame_info));
  });
}

void Streamer::DisconnectStream(uint32_t stream_index) { stream_infos_.erase(stream_index); }

void Streamer::RequestConfig(uint32_t config) {
  async::PostTask(loop_.dispatcher(),
                  [this, config]() { device_->SetCurrentConfiguration(config); });
}

void Streamer::RequestCapture(uint32_t stream, const std::string& path, bool wantImage,
                              CaptureResponse callback) {
  async::PostTask(
      loop_.dispatcher(), [this, stream, path, wantImage, callback = callback.share()]() mutable {
        if (stream >= NumConnectedStreams()) {
          callback(ZX_ERR_OUT_OF_RANGE, nullptr);
          return;
        }
        if (capture_ != nullptr) {
          callback(ZX_ERR_UNAVAILABLE, nullptr);  // another capture in progress
          return;
        }
        auto capture_result = Capture::Create(stream, path, wantImage, callback.share());
        if (capture_result.is_error()) {
          callback(capture_result.error(), nullptr);
          return;
        }
        capture_ = capture_result.take_value();
      });
}

uint32_t Streamer::NumConfigs() { return config_count_; };
uint32_t Streamer::ConnectedConfig() { return connected_config_index_; };
uint32_t Streamer::NumConnectedStreams() { return connected_stream_count_; };

}  // namespace camera
