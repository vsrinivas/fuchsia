// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_EXAMPLES_DEMO_STREAM_PROVIDER_MANAGER_STREAM_PROVIDER_H_
#define SRC_CAMERA_EXAMPLES_DEMO_STREAM_PROVIDER_MANAGER_STREAM_PROVIDER_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include "stream_provider.h"

class ManagerStreamProvider : public StreamProvider {
 public:
  ManagerStreamProvider() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}
  ~ManagerStreamProvider();
  static std::unique_ptr<StreamProvider> Create();
  fit::result<
      std::tuple<fuchsia::sysmem::ImageFormat_2, fuchsia::sysmem::BufferCollectionInfo_2, bool>,
      zx_status_t>
  ConnectToStream(fidl::InterfaceRequest<fuchsia::camera2::Stream> request,
                  uint32_t index) override;
  virtual std::string GetName() override { return "fuchsia.camera2.Manager service"; }

  // |fuchsia::camera2::Manager|
  void OnDeviceAvailable(int device_id, fuchsia::camera2::DeviceInfo description,
                         bool last_known_camera);
  void OnDeviceUnavailable(int device_id);
  void OnDeviceMuteChanged(int device_id, bool currently_muted);

 private:
  async::Loop loop_;
  fuchsia::camera2::ManagerPtr manager_;
  fuchsia::sysmem::AllocatorSyncPtr allocator_;
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection_;
  std::map<int, fuchsia::camera2::DeviceInfo> devices_;
  zx::event async_events_;
};

#endif  // SRC_CAMERA_EXAMPLES_DEMO_STREAM_PROVIDER_MANAGER_STREAM_PROVIDER_H_
