// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/camera-gym/stream_cycler.h"

#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

namespace camera {

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

// TODO(48506): Hard code stream ID
constexpr uint32_t kConfigId = 1;

void StreamCycler::WatchDevicesCallback(std::vector<fuchsia::camera3::WatchDevicesEvent> events) {
  for (auto& event : events) {
    if (event.is_added()) {
      // Connect to device.
      // TODO(48506) Properly detect expected device id.
      watcher_->ConnectToDevice(event.added(), device_.NewRequest(loop_.dispatcher()));

      // Watch for mute changes.
      device_->WatchMuteState(fit::bind_member(this, &StreamCycler::WatchMuteStateHandler));

      // Fetch camera configurations
      device_->GetConfigurations(
          [this](std::vector<fuchsia::camera3::Configuration> configurations) {
            configurations_ = std::move(configurations);

            ZX_ASSERT(configurations_.size() > kConfigId);
            ZX_ASSERT(!configurations_[kConfigId].streams.empty());
            device_->SetCurrentConfiguration(kConfigId);
            device_->WatchCurrentConfiguration([this](uint32_t index) {
              // TODO(42241) - In order to work around fxb/42241, all camera3 clients must connect
              // to their respective streams in sequence and without possibility of overlap. Since
              // the camera connection sequence requires a series of asynchronous steps, we must
              // daisy-chain from one complete stream connection to the next. This is why the
              // original simple loop does not work reliably at this time.

              // BEGIN: Daisy-chain work around for fxb/42241
              ConnectToStream(kConfigId, 0 /* stream_index */);
              // END: Daisy-chain work around for fxb/42241
            });
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

void StreamCycler::ConnectToStream(uint32_t config_index, uint32_t stream_index) {
  ZX_ASSERT(configurations_.size() > config_index);
  ZX_ASSERT(configurations_[config_index].streams.size() > stream_index);
  auto image_format = configurations_[config_index].streams[stream_index].image_format;

  // Connect to specific stream
  StreamInfo new_stream_info;
  stream_infos_.emplace(stream_index, std::move(new_stream_info));
  auto& stream = stream_infos_[stream_index].stream;
  auto stream_request = stream.NewRequest(loop_.dispatcher());

  // Allocate buffer collection
  fuchsia::sysmem::BufferCollectionTokenHandle token_orig;
  allocator_->AllocateSharedCollection(token_orig.NewRequest());
  stream->SetBufferCollection(std::move(token_orig));
  stream->WatchBufferCollection([this, image_format, stream_index,
                                 &stream](fuchsia::sysmem::BufferCollectionTokenHandle token_back) {
    if (add_collection_handler_) {
      auto& stream_info = stream_infos_[stream_index];
      stream_info.add_collection_handler_returned_value =
          add_collection_handler_(std::move(token_back), image_format);
    } else {
      token_back.BindSync()->Close();
    }

    // BEGIN: Daisy-chain work around for fxb/42241
    const uint32_t stream_count = configurations_[kConfigId].streams.size();
    uint32_t next_stream_index = stream_index + 1;
    if (next_stream_index < stream_count) {
      ConnectToStream(kConfigId, next_stream_index);
    }
    // END: Daisy-chain work around for fxb/42241

    // Kick start the stream
    stream->GetNextFrame([this, stream_index](fuchsia::camera3::FrameInfo frame_info) {
      OnNextFrame(stream_index, std::move(frame_info));
    });
  });

  device_->ConnectToStream(stream_index, std::move(stream_request));
}

void StreamCycler::OnNextFrame(uint32_t stream_index, fuchsia::camera3::FrameInfo frame_info) {
  if (show_buffer_handler_) {
    auto& stream_info = stream_infos_[stream_index];
    show_buffer_handler_(stream_info.add_collection_handler_returned_value, frame_info.buffer_index,
                         std::move(frame_info.release_fence));
  } else {
    frame_info.release_fence.reset();
  }
  auto& stream = stream_infos_[stream_index].stream;
  stream->GetNextFrame([this, stream_index](fuchsia::camera3::FrameInfo frame_info) {
    OnNextFrame(stream_index, std::move(frame_info));
  });
}

}  // namespace camera
