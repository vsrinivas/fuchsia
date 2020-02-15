// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/fake_camera/fake_camera_impl.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

#include "src/camera/lib/fake_stream/fake_stream_impl.h"

namespace camera {

fit::result<std::unique_ptr<FakeCamera>, zx_status_t> FakeCamera::Create(
    std::string identifier, std::vector<FakeConfiguration> configurations) {
  auto result = FakeCameraImpl::Create(std::move(identifier), std::move(configurations));
  if (result.is_error()) {
    return fit::error(result.error());
  }
  return fit::ok(result.take_value());
}

FakeCameraImpl::FakeCameraImpl() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

FakeCameraImpl::~FakeCameraImpl() {
  async::PostTask(loop_.dispatcher(), fit::bind_member(this, &FakeCameraImpl::OnDestruction));
  loop_.Quit();
  loop_.JoinThreads();
}

static zx_status_t Validate(const std::vector<FakeConfiguration>& configurations) {
  zx_status_t status = ZX_OK;

  if (configurations.empty()) {
    status = ZX_ERR_INVALID_ARGS;
    FX_PLOGS(ERROR, status) << "Configurations must not be empty.";
  }

  for (const auto& configuration : configurations) {
    if (configuration.streams.empty()) {
      status = ZX_ERR_INVALID_ARGS;
      FX_PLOGS(ERROR, status) << "Streams must not be empty.";
    }
    for (const auto& stream : configuration.streams) {
      if (!stream.stream) {
        status = ZX_ERR_INVALID_ARGS;
        FX_PLOGS(ERROR, status) << "Stream must be non-null.";
      }
    }
  }

  return status;
}

fit::result<std::unique_ptr<FakeCameraImpl>, zx_status_t> FakeCameraImpl::Create(
    std::string identifier, std::vector<FakeConfiguration> configurations) {
  auto camera = std::make_unique<FakeCameraImpl>();

  zx_status_t status = Validate(configurations);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Configurations failed validation.";
    return fit::error(status);
  }

  camera->identifier_ = std::move(identifier);
  camera->configurations_ = std::move(configurations);

  for (auto& configuration : camera->configurations_) {
    fuchsia::camera3::Configuration real_configuration;
    for (auto& stream : configuration.streams) {
      auto stream_impl = reinterpret_cast<FakeStreamImpl*>(stream.stream.get());
      real_configuration.streams.push_back(stream_impl->properties_);
    }
    camera->real_configurations_.push_back(std::move(real_configuration));
  }

  ZX_ASSERT(camera->loop_.StartThread("Fake Camera Loop") == ZX_OK);

  return fit::ok(std::move(camera));
}

fidl::InterfaceRequestHandler<fuchsia::camera3::Device> FakeCameraImpl::GetHandler() {
  return fit::bind_member(this, &FakeCameraImpl::OnNewRequest);
}

void FakeCameraImpl::SetHardwareMuteState(bool muted) { bindings_.CloseAll(ZX_ERR_NOT_SUPPORTED); }

void FakeCameraImpl::OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::Device> request) {
  if (bindings_.size() > 0) {
    request.Close(ZX_ERR_ALREADY_BOUND);
    return;
  }

  bindings_.AddBinding(this, std::move(request), loop_.dispatcher());
}

void FakeCameraImpl::OnDestruction() { bindings_.CloseAll(ZX_ERR_IO_NOT_PRESENT); }

template <class T>
void FakeCameraImpl::SetDisconnectErrorHandler(fidl::InterfacePtr<T>& p) {
  p.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Dependent component disconnected.";
    bindings_.CloseAll(ZX_ERR_INTERNAL);
  });
}

void FakeCameraImpl::GetIdentifier(GetIdentifierCallback callback) { callback(identifier_); }

void FakeCameraImpl::GetConfigurations(GetConfigurationsCallback callback) {
  callback(real_configurations_);
}

void FakeCameraImpl::WatchCurrentConfiguration(WatchCurrentConfigurationCallback callback) {
  bindings_.CloseAll(ZX_ERR_NOT_SUPPORTED);
}

void FakeCameraImpl::SetCurrentConfiguration(uint32_t index) {
  bindings_.CloseAll(ZX_ERR_NOT_SUPPORTED);
}

void FakeCameraImpl::WatchMuteState(WatchMuteStateCallback callback) {
  bindings_.CloseAll(ZX_ERR_NOT_SUPPORTED);
}

void FakeCameraImpl::SetSoftwareMuteState(bool muted, SetSoftwareMuteStateCallback callback) {
  bindings_.CloseAll(ZX_ERR_NOT_SUPPORTED);
}

void FakeCameraImpl::ConnectToStream(
    uint32_t index, fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
    fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  auto& afs = configurations_[current_configuration_index_].streams[index];
  afs.connection_callback(std::move(token));
  afs.stream->GetHandler()(std::move(request));
}

void FakeCameraImpl::Rebind(fidl::InterfaceRequest<Device> request) {
  bindings_.CloseAll(ZX_ERR_NOT_SUPPORTED);
}

}  // namespace camera
