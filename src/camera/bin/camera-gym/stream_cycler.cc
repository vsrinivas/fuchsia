// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/camera-gym/stream_cycler.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/types.h>

#include <sstream>

#include "src/camera/bin/camera-gym/moving_window.h"
#include "src/lib/fsl/handles/object_info.h"

namespace camera {

constexpr zx::duration kDemoTime = zx::msec(CONFIGURATION_CYCLE_TIME_MS);

// Ratio controls how often ROI is moved.
// (1 = every frame, 2 = every other frame, etc)
constexpr uint32_t kRegionOfInterestFramesPerMoveRatio = 1;

// Sets the error handler on the provided interface to log an error and abort the process.
template <class T>
static void SetAbortOnError(fidl::InterfacePtr<T>& p, std::string message) {
  p.set_error_handler([message](zx_status_t status) {
    // FATAL severity causes abort to be called.
    FX_PLOGS(FATAL, status) << message;
  });
}

StreamCycler::StreamCycler() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  SetAbortOnError(watcher_, "fuchsia.camera3.DeviceWatcher disconnected.");
  SetAbortOnError(allocator_, "fuchsia.sysmem.Allocator disconnected.");
  SetAbortOnError(device_, "fuchsia.camera3.Device disconnected.");
}

StreamCycler::~StreamCycler() {
  loop_.Quit();
  loop_.JoinThreads();
}

fit::result<std::unique_ptr<StreamCycler>, zx_status_t> StreamCycler::Create(
    fuchsia::camera3::DeviceWatcherHandle watcher, fuchsia::sysmem::AllocatorHandle allocator) {
  auto cycler = std::unique_ptr<StreamCycler>(new StreamCycler);

  zx_status_t status = cycler->watcher_.Bind(std::move(watcher), cycler->loop_.dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  status = cycler->allocator_.Bind(std::move(allocator), cycler->loop_.dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  cycler->watcher_->WatchDevices(
      fit::bind_member(cycler.get(), &StreamCycler::WatchDevicesCallback));

  status = cycler->loop_.StartThread("StreamCycler Thread");
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  return fit::ok(std::move(cycler));
}

void StreamCycler::SetHandlers(StreamCycler::AddCollectionHandler on_add_collection,
                               StreamCycler::RemoveCollectionHandler on_remove_collection,
                               StreamCycler::ShowBufferHandler on_show_buffer,
                               StreamCycler::MuteStateHandler on_mute_changed) {
  add_collection_handler_ = std::move(on_add_collection);
  remove_collection_handler_ = std::move(on_remove_collection);
  show_buffer_handler_ = std::move(on_show_buffer);
  mute_state_handler_ = std::move(on_mute_changed);
}

void StreamCycler::WatchDevicesCallback(std::vector<fuchsia::camera3::WatchDevicesEvent> events) {
  for (auto& event : events) {
    if (event.is_added()) {
      // Connect to device.
      // TODO(fxbug.dev/48506) Properly detect expected device id.
      watcher_->ConnectToDevice(event.added(), device_.NewRequest(loop_.dispatcher()));

      // Watch for mute changes.
      device_->WatchMuteState(fit::bind_member(this, &StreamCycler::WatchMuteStateHandler));

      // Fetch camera configurations
      device_->GetConfigurations(
          [this](std::vector<fuchsia::camera3::Configuration> configurations) {
            configurations_ = std::move(configurations);
            // Once we have the known camera configurations, default to the first configuration
            // index. This is automatically chosen in the driver, so we do not need to ask for it.
            // The callback for WatchCurrentConfiguration() will connect to all streams.
            device_->WatchCurrentConfiguration(
                fit::bind_member(this, &StreamCycler::WatchCurrentConfigurationCallback));
          });
    }
  }

  // Hanging get.
  watcher_->WatchDevices(fit::bind_member(this, &StreamCycler::WatchDevicesCallback));
}

void StreamCycler::WatchMuteStateHandler(bool software_muted, bool hardware_muted) {
  mute_state_handler_(software_muted | hardware_muted);
  device_->WatchMuteState(fit::bind_member(this, &StreamCycler::WatchMuteStateHandler));
}

void StreamCycler::ForceNextStreamConfiguration() {
  uint32_t config_index = NextConfigIndex();
  ZX_ASSERT(configurations_.size() > config_index);
  ZX_ASSERT(!configurations_[config_index].streams.empty());
  device_->SetCurrentConfiguration(config_index);
}

void StreamCycler::WatchCurrentConfigurationCallback(uint32_t config_index) {
  // Remember the current device config_index.
  current_config_index_ = config_index;

  // Start connecting to all streams.
  ConnectToAllStreams();

  // After a specified demo period, set the next stream configuration, which will end up cutting off
  // all existing streams.
  async::PostDelayedTask(
      loop_.dispatcher(), [this]() { ForceNextStreamConfiguration(); }, kDemoTime);

  // Be ready for configuration changes.
  device_->WatchCurrentConfiguration(
      fit::bind_member(this, &StreamCycler::WatchCurrentConfigurationCallback));
}

void StreamCycler::ConnectToAllStreams() {
  // Connect all streams.
  // TODO(fxbug.dev/42241) - In order to work around fxb/42241, all camera3 clients must connect to
  // their respective streams in sequence and without possibility of overlap. Since the camera
  // connection sequence requires a series of asynchronous steps, we must daisy-chain from one
  // complete stream connection to the next. This is why the original simple loop does not work
  // reliably at this time. This means that the following single ConnectToStream() will kick off all
  // the connections for all streams.
  uint32_t stream_index = 0;
  ConnectToStream(current_config_index_, stream_index);
}

void StreamCycler::ConnectToStream(uint32_t config_index, uint32_t stream_index) {
  ZX_ASSERT(configurations_.size() > config_index);
  ZX_ASSERT(configurations_[config_index].streams.size() > stream_index);
  auto image_format = configurations_[config_index].streams[stream_index].image_format;

  // Connect to specific stream
  StreamInfo new_stream_info;
  stream_infos_.emplace(stream_index, std::move(new_stream_info));
  auto& stream = stream_infos_[stream_index].stream;
  auto stream_request = stream.NewRequest(loop_.dispatcher());
  if (config_index == 1 || config_index == 2) {
    stream_infos_[stream_index].source_highlight = 0;
  }

  // Allocate buffer collection
  fuchsia::sysmem::BufferCollectionTokenHandle token_orig;
  allocator_->AllocateSharedCollection(token_orig.NewRequest());
  stream->SetBufferCollection(std::move(token_orig));
  stream->WatchBufferCollection([this, image_format, config_index, stream_index,
                                 &stream](fuchsia::sysmem::BufferCollectionTokenHandle token_back) {
    ZX_ASSERT(image_format.coded_width > 0);  // image_format must be reasonable.
    ZX_ASSERT(image_format.coded_height > 0);

    if (add_collection_handler_) {
      auto& stream_info = stream_infos_[stream_index];
      std::ostringstream oss;
      oss << "c" << config_index << "s" << stream_index << ".data";
      stream_info.add_collection_handler_returned_value =
          add_collection_handler_(std::move(token_back), image_format, oss.str());
    } else {
      token_back.BindSync()->Close();
    }

    // BEGIN: Daisy-chain work around for fxb/42241
    const uint32_t stream_count = configurations_[config_index].streams.size();
    uint32_t next_stream_index = stream_index + 1;
    if (next_stream_index < stream_count) {
      ConnectToStream(config_index, next_stream_index);
    }
    // END: Daisy-chain work around for fxb/42241

    // Kick start the stream
    stream->GetNextFrame([this, stream_index](fuchsia::camera3::FrameInfo frame_info) {
      OnNextFrame(stream_index, std::move(frame_info));
    });
  });

  device_->ConnectToStream(stream_index, std::move(stream_request));

  stream.set_error_handler(
      [this, stream_index](zx_status_t status) { DisconnectStream(stream_index); });
}

void StreamCycler::OnNextFrame(uint32_t stream_index, fuchsia::camera3::FrameInfo frame_info) {
  TRACE_DURATION("camera", "StreamCycler::OnNextFrame");
  TRACE_FLOW_END("camera", "camera3::Stream::GetNextFrame",
                 fsl::GetKoid(frame_info.release_fence.get()));
  auto& stream_info = stream_infos_[stream_index];
  if (show_buffer_handler_ && stream_info.add_collection_handler_returned_value) {
    show_buffer_handler_(stream_info.add_collection_handler_returned_value.value(),
                         frame_info.buffer_index, std::move(frame_info.release_fence),
                         stream_info.highlight);
  } else {
    frame_info.release_fence.reset();
  }
  auto& stream = stream_infos_[stream_index].stream;

  // Set the region of interest if appropriate.
  ZX_ASSERT(configurations_.size() > current_config_index_);
  auto& current_configuration = configurations_[current_config_index_];
  ZX_ASSERT(current_configuration.streams.size() > stream_index);
  auto& current_stream = current_configuration.streams[stream_index];
  if (current_stream.supports_crop_region) {
    const uint32_t limit = kRegionOfInterestFramesPerMoveRatio;
    static uint32_t count = 0;
    ++count;
    if (count >= limit) {
      count = 0;
      auto region = moving_window_.NextWindow();
      stream->SetCropRegion(std::make_unique<fuchsia::math::RectF>(region));
      if (stream_infos_[stream_index].source_highlight) {
        stream_infos_[stream_infos_[stream_index].source_highlight.value()].highlight = region;
      }
    }
  }

  stream->GetNextFrame([this, stream_index](fuchsia::camera3::FrameInfo frame_info) {
    OnNextFrame(stream_index, std::move(frame_info));
  });
}

void StreamCycler::DisconnectStream(uint32_t stream_index) {
  if (remove_collection_handler_) {
    auto& stream_info = stream_infos_[stream_index];
    if (stream_info.add_collection_handler_returned_value) {
      remove_collection_handler_(stream_info.add_collection_handler_returned_value.value());
    }
  }

  stream_infos_.erase(stream_index);
}

uint32_t StreamCycler::NextConfigIndex() {
  ZX_ASSERT(configurations_.size() > 0);
  ZX_ASSERT(current_config_index_ < configurations_.size());
  return (current_config_index_ + 1) % configurations_.size();
}

}  // namespace camera
