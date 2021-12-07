// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/camera-gym/stream_cycler.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <stdint.h>
#include <zircon/types.h>

#include <sstream>

#include "src/camera/bin/camera-gym/moving_window.h"
#include "src/lib/fsl/handles/object_info.h"

namespace camera {

using Command = fuchsia::camera::gym::Command;

using SetConfigCommand = fuchsia::camera::gym::SetConfigCommand;
using AddStreamCommand = fuchsia::camera::gym::AddStreamCommand;
using SetCropCommand = fuchsia::camera::gym::SetCropCommand;
using SetResolutionCommand = fuchsia::camera::gym::SetResolutionCommand;

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

StreamCycler::StreamCycler(async_dispatcher_t* dispatcher, bool manual_mode)
    : dispatcher_(dispatcher), manual_mode_(manual_mode) {
  SetAbortOnError(watcher_, "fuchsia.camera3.DeviceWatcher disconnected.");
  SetAbortOnError(allocator_, "fuchsia.sysmem.Allocator disconnected.");
  SetAbortOnError(device_, "fuchsia.camera3.Device disconnected.");
}

StreamCycler::~StreamCycler() = default;

fpromise::result<std::unique_ptr<StreamCycler>, zx_status_t> StreamCycler::Create(
    fuchsia::camera3::DeviceWatcherHandle watcher, fuchsia::sysmem::AllocatorHandle allocator,
    async_dispatcher_t* dispatcher, bool manual_mode) {
  auto cycler = std::unique_ptr<StreamCycler>(new StreamCycler(dispatcher, manual_mode));

  zx_status_t status = cycler->watcher_.Bind(std::move(watcher), dispatcher);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fpromise::error(status);
  }

  status = cycler->allocator_.Bind(std::move(allocator), dispatcher);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fpromise::error(status);
  }

  cycler->watcher_->WatchDevices(
      fit::bind_member(cycler.get(), &StreamCycler::WatchDevicesCallback));

  return fpromise::ok(std::move(cycler));
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
      watcher_->ConnectToDevice(event.added(), device_.NewRequest(dispatcher_));

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
  // Remember the current device config_index AFTER it has been set.
  current_config_index_ = config_index;

  if (manual_mode_) {
    // If manual mode, offer a done indication to command sequencer because the selection of a new
    // device configuration just finished, but do not do anything else.
    CommandSuccessNotify();
  } else {
    // If automatic mode, start all streams for current config, and start a timer to change to the
    // next configuration after kDemoTime.
    // Start connecting to all streams.
    ConnectToAllStreams();

    // After a specified demo period, set the next stream configuration, which will end up cutting
    // off all existing streams.
    async::PostDelayedTask(
        dispatcher_, [this]() { ForceNextStreamConfiguration(); }, kDemoTime);
  }

  // Be ready for configuration changes.
  device_->WatchCurrentConfiguration(
      fit::bind_member(this, &StreamCycler::WatchCurrentConfigurationCallback));
}

void StreamCycler::ConnectToAllStreams() {
  for (size_t i = 0; i < configurations_[current_config_index_].streams.size(); i++) {
    ConnectToStream(current_config_index_, i);
  }
}

void StreamCycler::ConnectToStream(uint32_t config_index, uint32_t stream_index) {
  ZX_ASSERT(configurations_.size() > config_index);
  ZX_ASSERT(configurations_[config_index].streams.size() > stream_index);
  auto image_format = configurations_[config_index].streams[stream_index].image_format;

  // Connect to specific stream
  StreamInfo new_stream_info;
  stream_infos_.emplace(stream_index, std::move(new_stream_info));
  auto& stream = stream_infos_[stream_index].stream;
  auto stream_request = stream.NewRequest(dispatcher_);
  if (config_index == 1 || config_index == 2) {
    stream_infos_[stream_index].source_highlight = 0;
  }

  // Allocate buffer collection
  fuchsia::sysmem::BufferCollectionTokenPtr token_orig;
  allocator_->AllocateSharedCollection(token_orig.NewRequest());

  stream->SetBufferCollection(std::move(token_orig));
  stream->WatchBufferCollection([this, image_format, config_index, stream_index,
                                 &stream](fuchsia::sysmem::BufferCollectionTokenHandle token_back) {
    ZX_ASSERT(image_format.coded_width > 0);  // image_format must be reasonable.
    ZX_ASSERT(image_format.coded_height > 0);
    ZX_ASSERT(image_format.bytes_per_row > 0);

    auto& stream_info = stream_infos_[stream_index];
    if (add_collection_handler_) {
      std::ostringstream oss;
      oss << "c" << config_index << "s" << stream_index << ".data";
      stream_info.add_collection_handler_returned_value =
          add_collection_handler_(std::move(token_back), image_format, oss.str());
    } else {
      token_back.BindSync()->Close();
    }

    // Initialize the current image format and the current coded size.
    stream_info.image_format = image_format;
    if (manual_mode_) {
      // If manual mode, offer a "ConnectToStream is done" indication to command sequencer.
      CommandSuccessNotify();
    }

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

  // If automatic mode, sequence through the crop region automatically.
  if (current_stream.supports_crop_region && !manual_mode_) {
    const uint32_t limit = kRegionOfInterestFramesPerMoveRatio;
    static uint32_t count = 0;
    ++count;
    if (count >= limit) {
      count = 0;
      auto region = moving_window_.NextWindow();
      stream->WatchCropRegion([this, stream_index](std::unique_ptr<fuchsia::math::RectF> region) {
        WatchCropRegionCallback(stream_index, std::move(region));
      });
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

void StreamCycler::WatchCropRegionCallback(uint32_t stream_index,
                                           std::unique_ptr<fuchsia::math::RectF> region) {
  CommandSuccessNotify();
}

void StreamCycler::CommandSuccessNotify() {
  CommandStatusHandler command_status_handler = std::move(command_status_handler_);
  if (command_status_handler) {
    ZX_ASSERT(controller_dispatcher_ != nullptr);
    async::PostTask(controller_dispatcher_,
                    [command_status_handler = std::move(command_status_handler)]() mutable {
                      fuchsia::camera::gym::Controller_SendCommand_Result result;
                      command_status_handler(result.WithResponse({}));
                    });
  }
}

void StreamCycler::CommandFailureNotify(::fuchsia::camera::gym::CommandError status) {
  CommandStatusHandler command_status_handler = std::move(command_status_handler_);
  if (command_status_handler) {
    ZX_ASSERT(controller_dispatcher_ != nullptr);
    async::PostTask(controller_dispatcher_, [command_status_handler =
                                                 std::move(command_status_handler)]() mutable {
      fuchsia::camera::gym::Controller_SendCommand_Result result;
      command_status_handler(result.WithErr(::fuchsia::camera::gym::CommandError::OUT_OF_RANGE));
    });
  }
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

void StreamCycler::ExecuteCommand(Command command, CommandStatusHandler handler) {
  async::PostTask(dispatcher_,
                  [this, command = std::move(command), handler = std::move(handler)]() mutable {
                    PostedExecuteCommand(std::move(command), std::move(handler));
                  });
}

void StreamCycler::PostedExecuteCommand(Command command, CommandStatusHandler handler) {
  ZX_ASSERT(!command_status_handler_);

  // TODO(b/180554943) - Allow for async in future. For now we only accept running one command at a
  // time. Individual commands below are assume to simply kick off the command, but the command
  // status handler must be called appropriately when the command has truly finished. This callback
  // is done using either CommandSuccessNotify or CommandFailureNotify.
  command_status_handler_ = std::move(handler);
  switch (command.Which()) {
    case Command::Tag::kSetConfig:
      ExecuteSetConfigCommand(command.set_config());
      break;
    case Command::Tag::kAddStream:
      ExecuteAddStreamCommand(command.add_stream());
      break;
    case Command::Tag::kSetCrop:
      ExecuteSetCropCommand(command.set_crop());
      break;
    case Command::Tag::kSetResolution:
      ExecuteSetResolutionCommand(command.set_resolution());
      break;
    default:
      ZX_ASSERT(false);
  }
}

// Actual execution of the "set-config" command.
void StreamCycler::ExecuteSetConfigCommand(SetConfigCommand& command) {
  uint32_t config_index = command.config_id;
  if (config_index >= configurations_.size()) {
    FX_LOGS(INFO) << "MANUAL MODE: ERROR: config_index " << config_index << " out of range";
    CommandFailureNotify(::fuchsia::camera::gym::CommandError::OUT_OF_RANGE);
    return;
  }

  // TODO(b/180555616) - This is not a great check. It is possible for another client to change the
  // configuration index for this camera device, and now "current_config_index_" is out-of-date, and
  // the test could behave incorrectly. It is assumed that the engineer running this test knows
  // exactly which clients have access to this camera device.
  if (current_config_index_ == config_index) {
    CommandSuccessNotify();
  }

  device_->SetCurrentConfiguration(config_index);
}

// Actual execution of the "add-stream" command.
void StreamCycler::ExecuteAddStreamCommand(AddStreamCommand& command) {
  uint32_t stream_index = command.stream_id;
  uint32_t config_index = current_config_index_;
  ZX_ASSERT(config_index < configurations_.size());
  if (stream_index >= configurations_[config_index].streams.size()) {
    FX_LOGS(INFO) << "MANUAL MODE: ERROR: stream_index " << stream_index
                  << " out of range for config_index " << config_index;
    CommandFailureNotify(::fuchsia::camera::gym::CommandError::OUT_OF_RANGE);
    return;
  }
  ConnectToStream(config_index, stream_index);
}

// Actual execution of the "set-crop" command.
void StreamCycler::ExecuteSetCropCommand(SetCropCommand& command) {
  uint32_t stream_index = command.stream_id;
  float x = command.x;
  float y = command.y;
  float width = command.width;
  float height = command.height;
  uint32_t config_index = current_config_index_;
  ZX_ASSERT(config_index < configurations_.size());
  if (stream_index >= configurations_[config_index].streams.size()) {
    FX_LOGS(INFO) << "MANUAL MODE: ERROR: stream_index " << stream_index
                  << " out of range for config_index " << config_index;
    CommandFailureNotify(::fuchsia::camera::gym::CommandError::OUT_OF_RANGE);
    return;
  }
  fuchsia::math::RectF region = {x, y, width, height};

  // TODO(b/180555730) - What if the stream_info has not been created yet?
  //                     What if the stream has not been connected yet?
  auto& stream = stream_infos_[stream_index].stream;

  stream->WatchCropRegion([this, stream_index](std::unique_ptr<fuchsia::math::RectF> region) {
    WatchCropRegionCallback(stream_index, std::move(region));
  });
  stream->SetCropRegion(std::make_unique<fuchsia::math::RectF>(region));
}

// Actual execution of the "set-resolution" command.
void StreamCycler::ExecuteSetResolutionCommand(SetResolutionCommand& command) {
  // TODO(fxb/60143) - Placeholder for set resolution command.
  CommandSuccessNotify();
}

}  // namespace camera
