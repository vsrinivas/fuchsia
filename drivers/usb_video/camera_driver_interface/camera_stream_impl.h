// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/camera/driver/cpp/fidl.h>

#include "lib/component/cpp/startup_context.h"

namespace camera_driver {

class CameraStreamImpl : public fuchsia::camera::driver::Stream {
 public:
  using GetFormatsCallback = fit::function<void(
      ::fidl::VectorPtr<fuchsia::camera::driver::VideoFormat>)>;
  using SetFormatsCallback = fit::function<void(uint32_t)>;

  CameraStreamImpl();

  void GetFormats(GetFormatsCallback callback);

  void SetFormats(
      fuchsia::camera::driver::VideoFormat format,
      ::fidl::InterfaceRequest<fuchsia::camera::driver::VideoBuffer> stream,
      SetFormatsCallback callback);

 private:
  class VideoBufferBinding : public fuchsia::camera::driver::VideoBuffer {
   public:
    static fbl::unique_ptr<VideoBufferBinding> Create(CameraStreamImpl* owner) {
      return fbl::unique_ptr<VideoBufferBinding>(new VideoBufferBinding(owner));
    }

    // VideoBuffer
    // TODO(CAM-1) Replace stubs with code
    void SetBuffer(::zx::vmo buffer) override {}
    void Start() override {}
    void Stop() override {}
    void FrameRelease(uint64_t data_offset) override {}

   private:
    friend class fbl::unique_ptr<VideoBufferBinding>;

    VideoBufferBinding(CameraStreamImpl* owner) : owner_(owner) {}
    ~VideoBufferBinding() override {}

    CameraStreamImpl* owner_;
  };

  CameraStreamImpl(const CameraStreamImpl&) = delete;
  CameraStreamImpl& operator=(const CameraStreamImpl&) = delete;

  std::unique_ptr<component::StartupContext> context_;
  fidl::BindingSet<Stream> bindings_;
  // Bindings for the Video Buffer connections:
  fidl::BindingSet<fuchsia::camera::driver::VideoBuffer,
                   fbl::unique_ptr<VideoBufferBinding>>
      video_buffer_bindings_;
};

}  // namespace camera_driver
