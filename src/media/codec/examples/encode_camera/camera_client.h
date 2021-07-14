// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_EXAMPLES_ENCODE_CAMERA_CAMERA_CLIENT_H_
#define SRC_MEDIA_CODEC_EXAMPLES_ENCODE_CAMERA_CAMERA_CLIENT_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <lib/fpromise/result.h>

// This class discovers a camera device and connects to the first stream on the first configuration
// and streams buffers to be shown.
class CameraClient {
 public:
  ~CameraClient();
  static fpromise::result<std::unique_ptr<CameraClient>, zx_status_t> Create(
      fuchsia::camera3::DeviceWatcherHandle watcher, fuchsia::sysmem::AllocatorHandle allocator,
      bool list_configs, uint32_t config_index, uint32_t stream_index);
  using AddCollectionHandler =
      fit::function<uint32_t(fuchsia::sysmem::BufferCollectionTokenHandle,
                             fuchsia::sysmem::ImageFormat_2, fuchsia::camera3::FrameRate)>;
  using RemoveCollectionHandler = fit::function<void(uint32_t)>;
  using ShowBufferHandler = fit::function<void(uint32_t, uint32_t, zx::eventpair)>;
  using MuteStateHandler = fit::function<void(bool)>;
  // Registers handlers that are called when the cycler adds or removes a buffer collection. The
  // value returned by |on_add_collection| will be subsequently passed to |on_remove_collection|.
  void SetHandlers(AddCollectionHandler on_add_collection,
                   RemoveCollectionHandler on_remove_collection, ShowBufferHandler on_show_buffer,
                   MuteStateHandler on_mute_changed);

 private:
  CameraClient(bool list_configs, uint32_t config_index, uint32_t stream_index);
  void WatchDevicesCallback(std::vector<fuchsia::camera3::WatchDevicesEvent> events);
  void WatchMuteStateHandler(bool software_muted, bool hardware_muted);
  void ConnectToStream(uint32_t config_index, uint32_t stream_index);
  void OnNextFrame(uint32_t stream_index, fuchsia::camera3::FrameInfo frame_info);
  void DumpConfigs();

  fuchsia::camera3::DeviceWatcherPtr watcher_;
  fuchsia::sysmem::AllocatorPtr allocator_;
  fuchsia::camera3::DevicePtr device_;
  std::vector<fuchsia::camera3::Configuration> configurations_;
  AddCollectionHandler add_collection_handler_;
  RemoveCollectionHandler remove_collection_handler_;
  ShowBufferHandler show_buffer_handler_;
  MuteStateHandler mute_state_handler_;

  bool list_configs_ = false;
  uint32_t config_index_ = 0;
  uint32_t stream_index_ = 0;

  // stream_infos_ uses the same index as the corresponding stream index in configurations_.
  struct StreamInfo {
    fuchsia::camera3::StreamPtr stream;
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info;
    uint32_t add_collection_handler_returned_value;
  };
  std::map<uint32_t, StreamInfo> stream_infos_;
};

#endif  // SRC_MEDIA_CODEC_EXAMPLES_ENCODE_CAMERA_CAMERA_CLIENT_H_
