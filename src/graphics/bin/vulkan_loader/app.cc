// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/bin/vulkan_loader/app.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/vfs/cpp/remote_dir.h>

#include "src/graphics/bin/vulkan_loader/goldfish_device.h"
#include "src/graphics/bin/vulkan_loader/icd_component.h"
#include "src/graphics/bin/vulkan_loader/magma_device.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"

LoaderApp::LoaderApp(sys::ComponentContext* context, async_dispatcher_t* dispatcher)
    : context_(context),
      dispatcher_(dispatcher),
      inspector_(context),
      device_fs_(dispatcher),
      manifest_fs_(dispatcher),
      fdio_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  fdio_loop_.StartThread("fdio_loop");
  inspector_.Health().StartingUp();
  devices_node_ = inspector_.root().CreateChild("devices");
  icds_node_ = inspector_.root().CreateChild("icds");
  manifest_fs_root_node_ = fbl::MakeRefCounted<fs::PseudoDir>();
}
LoaderApp::~LoaderApp() {}

zx_status_t LoaderApp::InitDeviceFs() {
  device_root_node_ = fbl::MakeRefCounted<fs::PseudoDir>();

  const char* kDevClassList[] = {"gpu", "goldfish-pipe", "goldfish-control",
                                 "goldfish-address-space", "goldfish-sync"};

  auto class_node = fbl::MakeRefCounted<fs::PseudoDir>();
  device_root_node_->AddEntry("class", class_node);

  for (const char* dev_class : kDevClassList) {
    fidl::InterfaceHandle<fuchsia::io::Directory> gpu_dir;
    std::string input_path = std::string("/dev/class/") + dev_class;
    zx_status_t status = fdio_open(
        input_path.c_str(), fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
        gpu_dir.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to open " << input_path;
      return status;
    }

    class_node->AddEntry(dev_class,
                         fbl::MakeRefCounted<fs::RemoteDir>(
                             fidl::ClientEnd<fuchsia_io::Directory>(gpu_dir.TakeChannel())));
  }

  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    return status;
  }
  auto devfs_out = std::make_unique<vfs::RemoteDir>(std::move(client));
  if ((status = ServeDeviceFs(std::move(server))) != ZX_OK) {
    return status;
  }
  context_->outgoing()->debug_dir()->AddEntry("device-fs", std::move(devfs_out));
  return ZX_OK;
}

zx_status_t LoaderApp::ServeDeviceFs(zx::channel dir_request) {
  auto options = fs::VnodeConnectionOptions::ReadWrite();
  return device_fs_.Serve(device_root_node_,
                          fidl::ServerEnd<fuchsia_io::Node>(std::move(dir_request)), options);
}

zx_status_t LoaderApp::ServeManifestFs(zx::channel dir_request) {
  auto options = fs::VnodeConnectionOptions::ReadWrite();
  return manifest_fs_.Serve(manifest_fs_root_node_,
                            fidl::ServerEnd<fuchsia_io::Node>(std::move(dir_request)), options);
}

zx_status_t LoaderApp::InitManifestFs() {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    return status;
  }
  auto devfs_out = std::make_unique<vfs::RemoteDir>(std::move(client));
  if ((status = ServeManifestFs(std::move(server))) != ZX_OK) {
    return status;
  }
  context_->outgoing()->debug_dir()->AddEntry("manifest-fs", std::move(devfs_out));
  return ZX_OK;
}

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
  std::lock_guard lock(pending_action_mutex_);
  NotifyIcdsChangedLocked();
}

void LoaderApp::NotifyIcdsChangedLocked() {
  if (icd_notification_pending_)
    return;
  icd_notification_pending_ = true;
  async::PostTask(dispatcher_, [this]() { this->NotifyIcdsChangedOnMainThread(); });
}

void LoaderApp::NotifyIcdsChangedOnMainThread() {
  FIT_DCHECK_IS_THREAD_VALID(main_thread_);
  {
    std::lock_guard lock(pending_action_mutex_);
    icd_notification_pending_ = false;
  }
  bool have_icd = false;
  for (auto& device : devices_) {
    have_icd |= device->icd_list().UpdateCurrentComponent();
  }
  if (have_icd) {
    inspector_.Health().Ok();
  }
  for (auto& observer : observer_list_)
    observer.OnIcdListChanged(this);
}

std::optional<zx::vmo> LoaderApp::GetMatchingIcd(const std::string& name) {
  FIT_DCHECK_IS_THREAD_VALID(main_thread_);
  for (auto& device : devices_) {
    auto res = device->icd_list().GetVmoMatchingSystemLib(name);
    if (res) {
      return std::optional<zx::vmo>(std::move(res));
    }
  }
  {
    std::lock_guard lock(pending_action_mutex_);
    // If not actions are pending then assume there will never be a match.
    if (pending_action_count_ == 0) {
      return zx::vmo();
    }
  }
  return {};
}

std::unique_ptr<LoaderApp::PendingActionToken> LoaderApp::GetPendingActionToken() {
  return std::unique_ptr<PendingActionToken>(new PendingActionToken(this));
}

LoaderApp::PendingActionToken::~PendingActionToken() {
  std::lock_guard lock(app_->pending_action_mutex_);
  if (--app_->pending_action_count_ == 0) {
    app_->NotifyIcdsChangedLocked();
  }
}
