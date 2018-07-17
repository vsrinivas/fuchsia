// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera_stream_impl.h"

#include "lib/component/cpp/startup_context.h"

namespace camera_driver {
using fuchsia::camera::driver::VideoBuffer;
using fuchsia::camera::driver::VideoFormat;

CameraStreamImpl::CameraStreamImpl()
    : context_(component::StartupContext::CreateFromStartupInfo()) {
  // TODO(CAM-1): do this with the ioctl call instead
  context_->outgoing().AddPublicService(bindings_.GetHandler(this));
}

void CameraStreamImpl::GetFormats(GetFormatsCallback callback) {
  // Stub: just send back an empty vector
  // TODO(CAM-1): fill this in.
  ::fidl::VectorPtr<VideoFormat> formats;
  callback(fbl::move(formats));
}

void CameraStreamImpl::SetFormats(VideoFormat format,
                                  ::fidl::InterfaceRequest<VideoBuffer> stream,
                                  SetFormatsCallback callback) {
  // Stub: connect the request, return 0 as max size:
  // TODO(CAM-1): fill this in.
  video_buffer_bindings_.AddBinding(VideoBufferBinding::Create(this),
                                    std::move(stream));
  callback(0);
}

}  // namespace camera_driver
