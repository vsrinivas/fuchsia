// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_CAMERA_GYM_STREAM_CYCLER_H_
#define SRC_CAMERA_BIN_CAMERA_GYM_STREAM_CYCLER_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>

#include <gtest/gtest_prod.h>

#include "fuchsia/math/cpp/fidl.h"
#include "src/camera/bin/camera-gym/moving_window.h"

namespace camera {

// This class is responsible for exercising the camera APIs to cycle between the various streams and
// configurations reported by a camera.
class StreamCycler {
 public:
  ~StreamCycler();
  static fit::result<std::unique_ptr<StreamCycler>, zx_status_t> Create(
      fuchsia::camera3::DeviceWatcherHandle watcher, fuchsia::sysmem::AllocatorHandle allocator,
      bool manual_mode);
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

 private:
  explicit StreamCycler(bool manual_mode = false);

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

  // Next camera frame on given stream is available.
  void OnNextFrame(uint32_t stream_index, fuchsia::camera3::FrameInfo frame_info);

  // Disconnect a single stream.
  void DisconnectStream(uint32_t stream_index);

  // Utility to return what the next config_index should be.
  uint32_t NextConfigIndex();

  async::Loop loop_;
  fuchsia::camera3::DeviceWatcherPtr watcher_;
  fuchsia::sysmem::AllocatorPtr allocator_;
  fuchsia::camera3::DevicePtr device_;
  std::vector<fuchsia::camera3::Configuration> configurations_;

  bool manual_mode_;

  // Only set by WatchCurrentConfigurationCallback().
  // Only used by ConnectToAllStreams() and NextConfigIndex().
  uint32_t current_config_index_;

  AddCollectionHandler add_collection_handler_;
  RemoveCollectionHandler remove_collection_handler_;
  ShowBufferHandler show_buffer_handler_;
  MuteStateHandler mute_state_handler_;

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
  };
  std::map<uint32_t, StreamInfo> stream_infos_;

  friend class CameraGymTest;
  FRIEND_TEST(CameraGymTest, PendingCollectionId);
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_CAMERA_GYM_STREAM_CYCLER_H_
