// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/bin/vulkan_loader/loader.h"

#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>

LoaderImpl::~LoaderImpl() { app_->RemoveObserver(this); }

// static
void LoaderImpl::Add(LoaderApp* app, const std::shared_ptr<sys::OutgoingDirectory>& outgoing) {
  outgoing->AddPublicService(fidl::InterfaceRequestHandler<fuchsia::vulkan::loader::Loader>(
      [app](fidl::InterfaceRequest<fuchsia::vulkan::loader::Loader> request) {
        auto impl = std::make_unique<LoaderImpl>(app);
        LoaderImpl* impl_ptr = impl.get();
        impl_ptr->bindings_.AddBinding(std::move(impl), std::move(request), nullptr);
      }));
}

// LoaderApp::Observer implementation.
void LoaderImpl::OnIcdListChanged(LoaderApp* app) {
  auto it = callbacks_.begin();
  while (it != callbacks_.end()) {
    std::optional<zx::vmo> vmo = app->GetMatchingIcd(it->first);
    if (!vmo) {
      ++it;
    } else {
      it->second(*std::move(vmo));
      it = callbacks_.erase(it);
    }
  }
  if (callbacks_.empty()) {
    app_->RemoveObserver(this);
  }
}

// fuchsia::vulkan::loader::Loader impl
void LoaderImpl::Get(std::string name, GetCallback callback) {
  // TODO(fxbug.dev/13078): Remove code to load from /system/lib.
  std::string load_path = "/system/lib/" + name;
  int fd;
  zx_status_t status =
      fdio_open_fd(load_path.c_str(),
                   fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_EXECUTABLE, &fd);
  if (status != ZX_OK) {
    AddCallback(std::move(name), std::move(callback));
    return;
  }
  zx::vmo vmo;
  status = fdio_get_vmo_exec(fd, vmo.reset_and_get_address());
  close(fd);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not clone vmo exec: " << status;
  }
  callback(std::move(vmo));
}

void LoaderImpl::ConnectToDeviceFs(zx::channel channel) { app_->ServeDeviceFs(std::move(channel)); }

void LoaderImpl::GetSupportedFeatures(GetSupportedFeaturesCallback callback) {
  fuchsia::vulkan::loader::Features features =
      fuchsia::vulkan::loader::Features::CONNECT_TO_DEVICE_FS |
      fuchsia::vulkan::loader::Features::GET;
  callback(features);
}

void LoaderImpl::AddCallback(std::string name, fit::function<void(zx::vmo)> callback) {
  std::optional<zx::vmo> vmo = app_->GetMatchingIcd(name);
  if (vmo) {
    callback(*std::move(vmo));
    return;
  }
  callbacks_.emplace_back(std::make_pair(std::move(name), std::move(callback)));
  if (callbacks_.size() == 1) {
    app_->AddObserver(this);
  }
}
