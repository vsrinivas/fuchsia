// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/fake_stream/fake_stream_impl.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

namespace camera {

// TODO(msandy): remove when fxr/361307 lands
template <class T>
static void CloseAllWithEpitaph(fidl::BindingSet<T>& bindings, zx_status_t epitaph_value) {
  for (const auto& binding : bindings.bindings()) {
    binding->Close(epitaph_value);
  }
  bindings.CloseAll();
}

fit::result<std::unique_ptr<FakeStream>, zx_status_t> FakeStream::Create(
    fuchsia::camera3::StreamProperties properties) {
  auto result = FakeStreamImpl::Create(std::move(properties));
  if (result.is_error()) {
    return fit::error(result.error());
  }
  return fit::ok(result.take_value());
}

FakeStreamImpl::FakeStreamImpl() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

FakeStreamImpl::~FakeStreamImpl() {
  async::PostTask(loop_.dispatcher(), fit::bind_member(this, &FakeStreamImpl::OnDestruction));
  loop_.Quit();
  loop_.JoinThreads();
}

static zx_status_t Validate(const fuchsia::camera3::StreamProperties& properties) {
  zx_status_t status = ZX_OK;
  if (properties.image_format.coded_width == 0 || properties.image_format.coded_height == 0 ||
      properties.image_format.bytes_per_row == 0) {
    status = ZX_ERR_INVALID_ARGS;
    FX_PLOGS(ERROR, status) << "Invalid image format dimensions or stride.";
  }
  if (properties.image_format.pixel_format.type == fuchsia::sysmem::PixelFormatType::INVALID) {
    status = ZX_ERR_INVALID_ARGS;
    FX_PLOGS(ERROR, status) << "Invalid pixel format type.";
  }
  if (properties.frame_rate.numerator == 0 || properties.frame_rate.denominator == 0) {
    status = ZX_ERR_INVALID_ARGS;
    FX_PLOGS(ERROR, status) << "Invalid frame rate.";
  }
  if (properties.supported_resolutions.empty()) {
    status = ZX_ERR_INVALID_ARGS;
    FX_PLOGS(ERROR, status) << "Supported resolutions must not be empty.";
  }
  for (const auto& resolution : properties.supported_resolutions) {
    if (resolution.coded_size.width == 0 || resolution.coded_size.height == 0 ||
        resolution.bytes_per_row == 0) {
      status = ZX_ERR_INVALID_ARGS;
      FX_PLOGS(ERROR, status) << "Invalid resolution or stride.";
    }
    if (static_cast<uint32_t>(resolution.coded_size.width) > properties.image_format.coded_width ||
        static_cast<uint32_t>(resolution.coded_size.height) >
            properties.image_format.coded_height) {
      status = ZX_ERR_INVALID_ARGS;
      FX_PLOGS(ERROR, status) << "Resolution too large for image format.";
    }
  }
  return status;
}

fit::result<std::unique_ptr<FakeStreamImpl>, zx_status_t> FakeStreamImpl::Create(
    fuchsia::camera3::StreamProperties properties) {
  auto stream = std::make_unique<FakeStreamImpl>();

  zx_status_t status = Validate(properties);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "StreamProperties failed validation.";
    return fit::error(status);
  }

  stream->properties_ = std::move(properties);

  ZX_ASSERT(stream->loop_.StartThread("Fake Stream Loop") == ZX_OK);

  return fit::ok(std::move(stream));
}

fidl::InterfaceRequestHandler<fuchsia::camera3::Stream> FakeStreamImpl::GetHandler() {
  return fit::bind_member(this, &FakeStreamImpl::OnNewRequest);
}

void FakeStreamImpl::AddFrame(fuchsia::camera3::FrameInfo info) {
  async::PostTask(loop_.dispatcher(), [this, info = std::move(info)]() mutable {
    if (frame_request_) {
      frame_request_(std::move(info));
      frame_request_ = nullptr;
      return;
    }
    frames_.push(std::move(info));
  });
}

void FakeStreamImpl::OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  if (bindings_.size() > 0) {
    request.Close(ZX_ERR_ALREADY_BOUND);
    return;
  }

  bindings_.AddBinding(this, std::move(request), loop_.dispatcher());
}

void FakeStreamImpl::OnDestruction() { CloseAllWithEpitaph(bindings_, ZX_ERR_IO_NOT_PRESENT); }

void FakeStreamImpl::SetCropRegion(std::unique_ptr<fuchsia::math::RectF> region) {
  CloseAllWithEpitaph(bindings_, ZX_ERR_NOT_SUPPORTED);
}

void FakeStreamImpl::WatchCropRegion(WatchCropRegionCallback callback) {
  CloseAllWithEpitaph(bindings_, ZX_ERR_NOT_SUPPORTED);
}

void FakeStreamImpl::SetResolution(uint32_t index) {
  CloseAllWithEpitaph(bindings_, ZX_ERR_NOT_SUPPORTED);
}

void FakeStreamImpl::WatchResolution(WatchResolutionCallback callback) {
  CloseAllWithEpitaph(bindings_, ZX_ERR_NOT_SUPPORTED);
}

void FakeStreamImpl::GetNextFrame(GetNextFrameCallback callback) {
  if (frame_request_) {
    CloseAllWithEpitaph(bindings_, ZX_ERR_BAD_STATE);
    return;
  }

  if (frames_.empty()) {
    frame_request_ = std::move(callback);
    return;
  }

  callback(std::move(frames_.front()));
  frames_.pop();
}

void FakeStreamImpl::Rebind(fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  request.Close(ZX_ERR_NOT_SUPPORTED);
  CloseAllWithEpitaph(bindings_, ZX_ERR_NOT_SUPPORTED);
}

}  // namespace camera
