// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/camera-gym/stream_cycler.h"

#include <lib/async-loop/default.h>
#include <zircon/types.h>

#include "src/lib/syslog/cpp/logger.h"

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
                               StreamCycler::ShowBufferHandler on_show_buffer) {
  add_collection_handler_ = std::move(on_add_collection);
  remove_collection_handler_ = std::move(on_remove_collection);
  show_buffer_handler_ = std::move(on_show_buffer);
}

// TODO(48506): Hard code stream ID
constexpr uint32_t kConfigId = 1;
constexpr uint32_t kStreamId = 2;

void StreamCycler::WatchDevicesCallback(std::vector<fuchsia::camera3::WatchDevicesEvent> events) {
  for (auto& event : events) {
    if (event.is_added()) {
      // Connect to device.
      // TODO(48506) Properly detect expected device id.
      watcher_->ConnectToDevice(event.added(), device_.NewRequest(loop_.dispatcher()));

      // Fetch camera configurations
      device_->GetConfigurations(
          [this](std::vector<fuchsia::camera3::Configuration> configurations) {
            // Assuming the configuration needed is actually there.
            ZX_ASSERT(configurations.size() > kConfigId);
            ZX_ASSERT(configurations[kConfigId].streams.size() > kStreamId);
            auto image_format = configurations[kConfigId].streams[kStreamId].image_format;

            // Connect to specific stream
            device_->SetCurrentConfiguration(kConfigId);
            auto stream_request = stream_.NewRequest(loop_.dispatcher());
            device_->WatchCurrentConfiguration(
                [this, stream_request = std::move(stream_request)](uint32_t index) mutable {
                  device_->ConnectToStream(kStreamId, std::move(stream_request));
                });

            // Allocate buffer collection
            fuchsia::sysmem::BufferCollectionTokenHandle token_orig;
            allocator_->AllocateSharedCollection(token_orig.NewRequest());
            stream_->SetBufferCollection(std::move(token_orig));
            stream_->WatchBufferCollection(
                [this, image_format](fuchsia::sysmem::BufferCollectionTokenHandle token_back) {
                  if (add_collection_handler_) {
                    // TODO(48506): support more than one stream
                    add_collection_handler_returned_value_ =
                        add_collection_handler_(std::move(token_back), image_format);
                  } else {
                    token_back.BindSync()->Close();
                  }
                  // Kick start the stream
                  stream_->GetNextFrame(fit::bind_member(this, &StreamCycler::OnNextFrame));
                });
          });
    }
  }

  // Hanging get.
  watcher_->WatchDevices(fit::bind_member(this, &StreamCycler::WatchDevicesCallback));
}

void StreamCycler::OnNextFrame(fuchsia::camera3::FrameInfo frame_info) {
  if (show_buffer_handler_) {
    // TODO(48506): support more than one stream
    show_buffer_handler_(add_collection_handler_returned_value_, frame_info.buffer_index,
                         std::move(frame_info.release_fence));
  } else {
    frame_info.release_fence.reset();
  }
  stream_->GetNextFrame(fit::bind_member(this, &StreamCycler::OnNextFrame));
}

}  // namespace camera
