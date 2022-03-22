// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_CAMERA_GYM_STREAM_CYCLER_H_
#define SRC_CAMERA_BIN_CAMERA_GYM_STREAM_CYCLER_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <lib/fpromise/result.h>

#include <gtest/gtest_prod.h>

#include "fuchsia/camera/gym/cpp/fidl.h"
#include "fuchsia/math/cpp/fidl.h"
#include "src/camera/bin/camera-gym/moving_window.h"

namespace camera {

// This class is responsible for exercising the camera APIs to cycle between the various streams and
// configurations reported by a camera.
class StreamCycler {
 public:
  ~StreamCycler();
  static fpromise::result<std::unique_ptr<StreamCycler>, zx_status_t> Create(
      fuchsia::camera3::DeviceWatcherHandle watcher, fuchsia::sysmem::AllocatorHandle allocator,
      async_dispatcher_t* dispatcher, bool manual_mode);
  using AddCollectionHandler = fit::function<uint32_t(fuchsia::sysmem::BufferCollectionTokenHandle,
                                                      fuchsia::sysmem::ImageFormat_2, std::string)>;
  using RemoveCollectionHandler = fit::function<void(uint32_t)>;
  using ShowBufferHandler =
      fit::function<void(uint32_t, uint32_t, zx::eventpair, std::optional<fuchsia::math::RectF>)>;
  using MuteStateHandler = fit::function<void(bool)>;
  // Registers handlers that are called when the cycler adds or removes a buffer collection. The
  // value returned by |on_add_collection| will be subsequently passed to |on_remove_collection|.
  void SetHandlers(AddCollectionHandler on_add_collection,
                   RemoveCollectionHandler on_remove_collection, ShowBufferHandler on_show_buffer,
                   MuteStateHandler on_mute_changed);

  using CommandStatusHandler =
      fit::function<void(fuchsia::camera::gym::Controller_SendCommand_Result)>;

  void set_controller_dispatcher(async_dispatcher_t* dispatcher) {
    controller_dispatcher_ = dispatcher;
  }

  // Manual mode entry points:
  void ExecuteCommand(fuchsia::camera::gym::Command command, CommandStatusHandler handler);

 private:
  explicit StreamCycler(async_dispatcher_t* dispatcher, bool manual_mode = false);

  // Notification to camera-gym that the camera device is present.
  void WatchDevicesCallback(std::vector<fuchsia::camera3::WatchDevicesEvent> events);

  // Notification to camera-gym that the mute state has changed.
  void WatchMuteStateHandler(bool software_muted, bool hardware_muted);

  // Forcibly select the stream config for the next demo cycle. This has an intended side effect of
  // disconnecting all current streams, which should trigger the RemoveCollectionHandler's for the
  // existing streams.
  void ForceNextStreamConfiguration();

  // Notification to camera-gym that the stream configuration has changed.
  void WatchCurrentConfigurationCallback(uint32_t config_index);

  // Kick off the sequence to connect all of the streams for this demo cycle.
  void ConnectToAllStreams();

  // Kick off the sequence to connect a single stream.
  void ConnectToStream(uint32_t config_index, uint32_t stream_index);

  // Notification to camera-gym that the crop region has been set.
  void WatchCropRegionCallback(uint32_t stream_index, std::unique_ptr<fuchsia::math::RectF> region);

  // Next camera frame on given stream is available.
  void OnNextFrame(uint32_t stream_index, fuchsia::camera3::FrameInfo frame_info);

  // Disconnect a single stream.
  void DisconnectStream(uint32_t stream_index);

  // Utility to return what the next config_index should be.
  uint32_t NextConfigIndex();

  // Manual mode entry points:
  void PostedExecuteCommand(fuchsia::camera::gym::Command command, CommandStatusHandler handler);

  void ExecuteSetConfigCommand(fuchsia::camera::gym::SetConfigCommand& command);
  void ExecuteAddStreamCommand(fuchsia::camera::gym::AddStreamCommand& command);
  void ExecuteSetCropCommand(fuchsia::camera::gym::SetCropCommand& command);
  void ExecuteSetResolutionCommand(fuchsia::camera::gym::SetResolutionCommand& command);

  // When a command successfully executes, CommandSuccessNotify must be called.
  void CommandSuccessNotify();

  // When a command experiences any failure in execution, CommandFailureNotify must be called.
  void CommandFailureNotify(::fuchsia::camera::gym::CommandError status);

  async_dispatcher_t* dispatcher_;
  async_dispatcher_t* controller_dispatcher_;
  fuchsia::camera3::DeviceWatcherPtr watcher_;
  fuchsia::sysmem::AllocatorPtr allocator_;
  fuchsia::camera3::DevicePtr device_;
  std::vector<fuchsia::camera3::Configuration> configurations_;

  bool manual_mode_;

  // Only set by WatchCurrentConfigurationCallback().
  // Only used by ConnectToAllStreams() and NextConfigIndex().
  // Set to the config_index AFTER being notified that the config was set by the driver stack.
  uint32_t current_config_index_;

  AddCollectionHandler add_collection_handler_;
  RemoveCollectionHandler remove_collection_handler_;
  ShowBufferHandler show_buffer_handler_;
  MuteStateHandler mute_state_handler_;

  // TODO(?????) - Is this really the ideal way to communicate status back?
  CommandStatusHandler command_status_handler_;

  // Track the moving region of interest
  MovingWindow moving_window_;

  // stream_infos_ uses the same index as the corresponding stream index in configurations_.
  struct StreamInfo {
    fuchsia::camera3::StreamPtr stream;
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info;
    std::optional<uint32_t> add_collection_handler_returned_value;
    std::optional<uint32_t>
        source_highlight;  // Stream on which to highlight this stream's crop region.
    std::optional<fuchsia::math::RectF> highlight;
    fuchsia::sysmem::ImageFormat_2 image_format;
  };
  std::map<uint32_t, StreamInfo> stream_infos_;

  friend class CameraGymTest;
  friend class CameraGymStreamCyclerTest;
  FRIEND_TEST(CameraGymTest, PendingCollectionId);
  FRIEND_TEST(StreamCyclerTest, SimpleConfiguration_ManualMode_ConnectToStream);
  FRIEND_TEST(StreamCyclerTest, ComplexConfiguration_ManualMode_WatchCurrentConfigurationCallback);
  FRIEND_TEST(StreamCyclerTest, ComplexConfiguration_ManualMode_ExecuteSetConfigCommand_SameConfig);
  FRIEND_TEST(StreamCyclerTest,
              ComplexConfiguration_ManualMode_ExecuteSetConfigCommand_DifferentConfig);
  FRIEND_TEST(StreamCyclerTest, ComplexConfiguration_ManualMode_ExecuteAddStreamCommand);
  FRIEND_TEST(StreamCyclerTest, ComplexConfiguration_ManualMode_ExecuteSetCropCommand);
  FRIEND_TEST(StreamCyclerTest, CommandSuccessNotify);
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_CAMERA_GYM_STREAM_CYCLER_H_
