// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_FAKE_STREAM_FAKE_STREAM_IMPL_H_
#define SRC_CAMERA_LIB_FAKE_STREAM_FAKE_STREAM_IMPL_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>

#include <queue>

#include "src/camera/lib/fake_stream/fake_stream.h"

namespace camera {

class FakeCameraImpl;

// Implements the FakeStream interface. Unless otherwise noted, all public methods are thread-safe,
// and all private methods must be called on the loop's thread.
class FakeStreamImpl : public FakeStream, public fuchsia::camera3::Stream {
 public:
  FakeStreamImpl();
  ~FakeStreamImpl() override;
  static fit::result<std::unique_ptr<FakeStreamImpl>, zx_status_t> Create(
      fuchsia::camera3::StreamProperties properties,
      fit::function<void(fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>)>
          on_set_buffer_collection);
  fidl::InterfaceRequestHandler<fuchsia::camera3::Stream> GetHandler() override;
  void AddFrame(fuchsia::camera3::FrameInfo info) override;

 private:
  void OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::Stream> request);
  void OnDestruction();

  // |fuchsia::camera3::Stream|
  void SetCropRegion(std::unique_ptr<fuchsia::math::RectF> region) override;
  void WatchCropRegion(WatchCropRegionCallback callback) override;
  void SetResolution(fuchsia::math::Size coded_size) override;
  void WatchResolution(WatchResolutionCallback callback) override;
  void SetBufferCollection(
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) override;
  void WatchBufferCollection(WatchBufferCollectionCallback callback) override;
  void GetNextFrame(GetNextFrameCallback callback) override;
  void Rebind(fidl::InterfaceRequest<Stream> request) override;

  async::Loop loop_;
  fidl::BindingSet<fuchsia::camera3::Stream> bindings_;
  fuchsia::camera3::StreamProperties properties_;
  fit::function<void(fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>)>
      on_set_buffer_collection_;
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_;
  WatchBufferCollectionCallback token_request_;
  std::queue<fuchsia::camera3::FrameInfo> frames_;
  GetNextFrameCallback frame_request_;

  friend class FakeCameraImpl;
};

}  // namespace camera

#endif  // SRC_CAMERA_LIB_FAKE_STREAM_FAKE_STREAM_IMPL_H_
