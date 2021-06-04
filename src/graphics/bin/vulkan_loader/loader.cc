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
  if (!app->HavePendingActions()) {
    for (auto& handle : connect_manifest_handles_) {
      app_->ServeManifestFs(std::move(handle));
    }
    connect_manifest_handles_.clear();
  }
  if (!waiting_for_callbacks()) {
    app_->RemoveObserver(this);
  }
}

// fuchsia::vulkan::loader::Loader impl
void LoaderImpl::Get(std::string name, GetCallback callback) {
  AddCallback(std::move(name), std::move(callback));
}

void LoaderImpl::ConnectToDeviceFs(zx::channel channel) { app_->ServeDeviceFs(std::move(channel)); }

void LoaderImpl::ConnectToManifestFs(fuchsia::vulkan::loader::ConnectToManifestOptions options,
                                     zx::channel channel) {
  if (!(options & fuchsia::vulkan::loader::ConnectToManifestOptions::WAIT_FOR_IDLE) ||
      !app_->HavePendingActions()) {
    app_->ServeManifestFs(std::move(channel));
    return;
  }

  bool was_waiting_for_callbacks = waiting_for_callbacks();
  connect_manifest_handles_.push_back(std::move(channel));
  if (waiting_for_callbacks() && !was_waiting_for_callbacks) {
    app_->AddObserver(this);
  }
}

void LoaderImpl::GetSupportedFeatures(GetSupportedFeaturesCallback callback) {
  fuchsia::vulkan::loader::Features features =
      fuchsia::vulkan::loader::Features::CONNECT_TO_DEVICE_FS |
      fuchsia::vulkan::loader::Features::GET |
      fuchsia::vulkan::loader::Features::CONNECT_TO_MANIFEST_FS;
  callback(features);
}

void LoaderImpl::AddCallback(std::string name, fit::function<void(zx::vmo)> callback) {
  bool was_waiting_for_callbacks = waiting_for_callbacks();
  std::optional<zx::vmo> vmo = app_->GetMatchingIcd(name);
  if (vmo) {
    callback(*std::move(vmo));
    return;
  }
  callbacks_.emplace_back(std::make_pair(std::move(name), std::move(callback)));
  if (waiting_for_callbacks() && !was_waiting_for_callbacks) {
    app_->AddObserver(this);
  }
}
