// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_FACTORY_STREAMER_H_
#define SRC_CAMERA_BIN_FACTORY_STREAMER_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>

#include <map>

#include "src/camera/bin/factory/capture.h"

namespace camera {

// A class that manages a video stream from a camera
class Streamer {
 public:
  Streamer();
  ~Streamer();

  // Make a streamer and start its thread
  static fit::result<std::unique_ptr<Streamer>, zx_status_t> Create(
      fuchsia::sysmem::AllocatorHandle allocator, fuchsia::camera3::DeviceWatcherHandle watcher,
      fit::closure stop_callback = nullptr);

  // once connected to device, return the number of available configs
  uint32_t NumConfigs();

  // if connected, return the connected config index
  uint32_t ConnectedConfig();

  // if connected to a config, return the number of currently connected streams (all are attempted)
  uint32_t NumConnectedStreams();

  // request a switch to another config index
  void RequestConfig(uint32_t config);

  // request a frame capture; the next available frame will be written to flash
  void RequestCapture(uint32_t stream, const std::string& path, bool wantImage,
                      CaptureResponse response);

 private:
  // Start the event loop
  zx_status_t Start();

  // Chain of calls to connect to streams and start receiving frames
  void WatchDevicesCallback(std::vector<fuchsia::camera3::WatchDevicesEvent> events);
  void WatchCurrentConfigurationCallback(uint32_t config_index);
  void ConnectToAllStreams();
  void ConnectToStream(uint32_t config_index, uint32_t stream_index);
  void OnNextFrame(uint32_t stream_index, fuchsia::camera3::FrameInfo frame_info);
  void DisconnectStream(uint32_t stream_index);
  void WatchBufferCollectionCallback(uint32_t config_index, uint32_t stream_index,
                                     fuchsia::sysmem::BufferCollectionTokenHandle token_back);
  void SyncCallback(uint32_t stream_index, fuchsia::sysmem::BufferCollectionTokenHandle token_back);
  void WaitForBuffersAllocatedCallback(uint32_t stream_index, zx_status_t status,
                                       fuchsia::sysmem::BufferCollectionInfo_2 buffers);

  async::Loop loop_;
  fit::closure stop_callback_;
  fuchsia::sysmem::AllocatorPtr allocator_;
  fuchsia::camera3::DeviceWatcherPtr watcher_;
  fuchsia::camera3::DevicePtr device_;
  std::vector<fuchsia::camera3::Configuration> configurations_;

  uint32_t config_count_ = 0;
  uint32_t connected_stream_count_ = 0;
  uint32_t connected_config_index_ = 0;
  uint32_t frame_count_ = 0;

  struct StreamInfo {
    fuchsia::sysmem::BufferCollectionTokenPtr token_ptr;
    fuchsia::sysmem::BufferCollectionPtr collection;
    fuchsia::sysmem::BufferCollectionInfo_2 collection_info;
    fuchsia::camera3::StreamPtr stream;
  };
  // stream_infos_ uses the same index as the corresponding stream index in configurations_.
  std::map<uint32_t, StreamInfo> stream_infos_;

  std::unique_ptr<Capture> capture_;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_FACTORY_STREAMER_H_
