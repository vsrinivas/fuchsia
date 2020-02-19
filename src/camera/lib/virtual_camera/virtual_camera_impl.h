// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_VIRTUAL_CAMERA_VIRTUAL_CAMERA_IMPL_H_
#define SRC_CAMERA_LIB_VIRTUAL_CAMERA_VIRTUAL_CAMERA_IMPL_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/clock.h>

#include <queue>

#include "src/camera/lib/fake_camera/fake_camera.h"
#include "src/camera/lib/virtual_camera/virtual_camera.h"

namespace camera {

class VirtualCameraImpl : public VirtualCamera {
 public:
  VirtualCameraImpl();
  ~VirtualCameraImpl() override;
  static fit::result<std::unique_ptr<VirtualCamera>, zx_status_t> Create(
      fidl::InterfaceHandle<fuchsia::sysmem::Allocator> allocator);
  fidl::InterfaceRequestHandler<fuchsia::camera3::Device> GetHandler() override;
  fit::result<void, std::string> CheckFrame(const void* data, size_t size,
                                            const fuchsia::camera3::FrameInfo& info) override;

 private:
  void OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::Device> request);
  void OnDestruction();
  void OnStreamConnected(fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token);
  void FrameTick();
  void FillFrame(uint32_t buffer_index);
  void EmbedMetadata(const fuchsia::camera3::FrameInfo& info);

  async::Loop loop_;
  fuchsia::sysmem::AllocatorPtr allocator_;
  std::unique_ptr<FakeCamera> camera_;
  std::shared_ptr<FakeStream> stream_;
  std::optional<fuchsia::sysmem::BufferCollectionInfo_2> buffers_;
  zx::time interrupt_start_time_;
  uint64_t frame_count_;
  std::queue<uint32_t> free_buffers_;
  std::map<uint32_t, std::unique_ptr<async::Wait>> frame_waiters_;
};

}  // namespace camera

#endif  // SRC_CAMERA_LIB_VIRTUAL_CAMERA_VIRTUAL_CAMERA_IMPL_H_
