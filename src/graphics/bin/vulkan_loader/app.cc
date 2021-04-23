// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/bin/vulkan_loader/app.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>

#include "src/graphics/bin/vulkan_loader/goldfish_device.h"
#include "src/graphics/bin/vulkan_loader/icd_component.h"
#include "src/graphics/bin/vulkan_loader/magma_device.h"

LoaderApp::LoaderApp(sys::ComponentContext* context, async_dispatcher_t* dispatcher)
    : context_(context),
      dispatcher_(dispatcher),
      inspector_(context),
      fdio_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  fdio_loop_.StartThread("fdio_loop");
  inspector_.Health().StartingUp();
  devices_node_ = inspector_.root().CreateChild("devices");
  icds_node_ = inspector_.root().CreateChild("icds");
}
LoaderApp::~LoaderApp() {}

zx_status_t LoaderApp::InitDeviceWatcher() {
  FIT_DCHECK_IS_THREAD_VALID(main_thread_);
  auto gpu_watcher_token = GetPendingActionToken();
  gpu_watcher_ = fsl::DeviceWatcher::CreateWithIdleCallback(
      "/dev/class/gpu",
      [this](int dir_fd, const std::string filename) {
        if (filename == ".") {
          return;
        }
        auto device = MagmaDevice::Create(this, dir_fd, filename, &devices_node_);
        if (device) {
          devices_.emplace_back(std::move(device));
        }
      },
      [gpu_watcher_token = std::move(gpu_watcher_token)]() {
        // Idle callback and gpu_watcher_token will be destroyed on idle.
      });
  if (!gpu_watcher_)
    return ZX_ERR_INTERNAL;
  auto goldfish_watcher_token = GetPendingActionToken();
  goldfish_watcher_ = fsl::DeviceWatcher::CreateWithIdleCallback(
      "/dev/class/goldfish-pipe",
      [this](int dir_fd, const std::string filename) {
        if (filename == ".") {
          return;
        }
        auto device = GoldfishDevice::Create(this, dir_fd, filename, &devices_node_);
        if (device) {
          devices_.emplace_back(std::move(device));
        }
      },
      [goldfish_watcher_token = std::move(goldfish_watcher_token)]() {
        // Idle callback and goldfish_watcher_token will be destroyed on idle.
      });
  if (!goldfish_watcher_)
    return ZX_ERR_INTERNAL;
  return ZX_OK;
}

void LoaderApp::RemoveDevice(GpuDevice* device) {
  FIT_DCHECK_IS_THREAD_VALID(main_thread_);
  auto it = std::remove_if(devices_.begin(), devices_.end(),
                           [device](const std::unique_ptr<GpuDevice>& unique_device) {
                             return device == unique_device.get();
                           });
  devices_.erase(it, devices_.end());
}

std::shared_ptr<IcdComponent> LoaderApp::CreateIcdComponent(std::string component_url) {
  FIT_DCHECK_IS_THREAD_VALID(main_thread_);
  if (icd_components_.find(component_url) != icd_components_.end())
    return icd_components_[component_url];
  icd_components_[component_url] = IcdComponent::Create(context_, this, &icds_node_, component_url);
  return icd_components_[component_url];
}

void LoaderApp::NotifyIcdsChanged() {
  // This can be called on any thread.
  if (!icd_notification_pending_.exchange(true)) {
    async::PostTask(dispatcher_, [this]() { this->NotifyIcdsChangedOnMainThread(); });
  }
}

void LoaderApp::NotifyIcdsChangedOnMainThread() {
  FIT_DCHECK_IS_THREAD_VALID(main_thread_);
  icd_notification_pending_ = false;
  for (auto& observer : observer_list_)
    observer.OnIcdListChanged(this);
}

std::optional<zx::vmo> LoaderApp::GetMatchingIcd(const std::string& name) {
  FIT_DCHECK_IS_THREAD_VALID(main_thread_);
  for (auto& device : devices_) {
    auto res = device->icd_list().GetVmoMatchingSystemLib(name);
    if (res) {
      inspector_.Health().Ok();
      return std::optional<zx::vmo>(std::move(res));
    }
  }
  // If not actions are pending then assume there will never be a match.
  if (pending_action_count_ == 0) {
    return zx::vmo();
  }
  return {};
}

std::unique_ptr<LoaderApp::PendingActionToken> LoaderApp::GetPendingActionToken() {
  return std::unique_ptr<PendingActionToken>(new PendingActionToken(this));
}

LoaderApp::PendingActionToken::~PendingActionToken() {
  if (--app_->pending_action_count_ == 0) {
    app_->NotifyIcdsChanged();
  }
}
