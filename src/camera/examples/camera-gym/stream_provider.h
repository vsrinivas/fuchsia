// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_EXAMPLES_CAMERA_GYM_STREAM_PROVIDER_H_
#define SRC_CAMERA_EXAMPLES_CAMERA_GYM_STREAM_PROVIDER_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>  // TODO(48124) - camera2 going away
#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/result.h>
#include <lib/sys/cpp/component_context.h>

#include <set>
#include <tuple>

#include <src/camera/bin/device/device_impl.h>

namespace camera {

class StreamProvider {
 public:
  // TODO(48124) - camera2 going away
  using FrameCallback = fit::function<void(fuchsia::camera2::FrameAvailableInfo)>;

  explicit StreamProvider(async::Loop* loop);
  ~StreamProvider();

  std::string GetName();
  zx_status_t Initialize();

  fit::result<std::tuple<fuchsia::sysmem::ImageFormat_2, fuchsia::sysmem::BufferCollectionInfo_2>,
              zx_status_t>
  ConnectToStream(FrameCallback frame_callback);

  zx_status_t PostGetNextFrame();
  zx_status_t GetNextFrame();
  zx_status_t SaveReleaseInfo(uint32_t buffer_id, fuchsia::camera3::FrameInfo* frame_info3);
  zx_status_t ReleaseFrame(uint32_t buffer_id);

  static std::unique_ptr<StreamProvider> Create(async::Loop* loop);

 private:
  async::Loop* loop_;
  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::camera3::DeviceWatcherSyncPtr watcher_ptr_;
  std::vector<uint64_t> cameras_;
  fuchsia::sysmem::AllocatorSyncPtr allocator_ptr_;
  fuchsia::camera3::DeviceSyncPtr device_ptr_;
  std::unique_ptr<DeviceImpl> device_impl_;
  fuchsia::camera3::StreamSyncPtr stream_ptr_;
  fuchsia::sysmem::ImageFormat_2 format_;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info_;
  FrameCallback frame_callback_;
  zx::event async_events_;

  std::vector<fuchsia::camera3::Configuration> configurations_;

  struct SaveFence {
    bool valid;
    zx::eventpair release_fence;
  };

  std::map<uint32_t, SaveFence> release_fences;
};

}  // namespace camera

#endif  // SRC_CAMERA_EXAMPLES_CAMERA_GYM_STREAM_PROVIDER_H_
