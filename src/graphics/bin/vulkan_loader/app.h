// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_BIN_VULKAN_LOADER_APP_H_
#define SRC_GRAPHICS_BIN_VULKAN_LOADER_APP_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/defer.h>
#include <lib/fit/thread_checker.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>

#include <list>
#include <unordered_map>

#include "src/graphics/bin/vulkan_loader/gpu_device.h"
#include "src/lib/fsl/io/device_watcher.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/observer_list.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"

class MagmaDevice;
class IcdComponent;

class LoaderApp {
 public:
  class Observer {
   public:
    // Called if the ICD list may have changed.
    virtual void OnIcdListChanged(LoaderApp* app) = 0;
  };

  // This token represents the existence of an outstanding operation that could
  // affect the ICD list. It will defer the signaling that an ICD doesn't exist
  // until it's destroyed.
  class PendingActionToken {
   public:
    ~PendingActionToken();

   private:
    friend class LoaderApp;

    explicit PendingActionToken(LoaderApp* app) : app_(app) {
      std::lock_guard lock(app->pending_action_mutex_);
      app->pending_action_count_++;
    }

    LoaderApp* app_;

    FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(PendingActionToken);
  };

  explicit LoaderApp(sys::ComponentContext* context, async_dispatcher_t* dispatcher);

  ~LoaderApp();

  zx_status_t InitDeviceWatcher();

  zx_status_t InitDeviceFs();
  zx_status_t InitManifestFs();
  zx_status_t ServeDeviceFs(zx::channel dir_request);
  zx_status_t ServeManifestFs(zx::channel dir_request);

  std::shared_ptr<IcdComponent> CreateIcdComponent(std::string component_url);

  void AddDevice(std::unique_ptr<GpuDevice> device) { devices_.push_back(std::move(device)); }
  void RemoveDevice(GpuDevice* device);

  // Notify observers that an ICD list has changed.
  void NotifyIcdsChanged();

  void AddObserver(Observer* obs) { observer_list_.AddObserver(obs); }
  void RemoveObserver(Observer* obs) { observer_list_.RemoveObserver(obs); }

  // Returns an ICD vmo that matches system_lib_name.
  std::optional<zx::vmo> GetMatchingIcd(const std::string& system_lib_name);

  size_t device_count() const { return devices_.size(); }

  async_dispatcher_t* fdio_loop_dispatcher() { return fdio_loop_.dispatcher(); }

  std::unique_ptr<PendingActionToken> GetPendingActionToken();

  fbl::RefPtr<fs::PseudoDir> manifest_fs_root_node() { return manifest_fs_root_node_; }

  bool HavePendingActions() const {
    std::lock_guard lock(pending_action_mutex_);
    return pending_action_count_ > 0 || icd_notification_pending_;
  }

 private:
  friend class LoaderActionToken;
  void NotifyIcdsChangedOnMainThread();
  void NotifyIcdsChangedLocked() FXL_REQUIRE(pending_action_mutex_);

  FIT_DECLARE_THREAD_CHECKER(main_thread_);

  sys::ComponentContext* context_;
  async_dispatcher_t* dispatcher_;
  sys::ComponentInspector inspector_;
  inspect::Node devices_node_;

  inspect::Node icds_node_;

  mutable std::mutex pending_action_mutex_;
  bool icd_notification_pending_ FXL_GUARDED_BY(pending_action_mutex_) = false;

  // Keep track of the number of pending operations that have the potential to modify the tree.
  uint64_t pending_action_count_ FXL_GUARDED_BY(pending_action_mutex_) = 0;

  fs::SynchronousVfs device_fs_;
  fbl::RefPtr<fs::PseudoDir> device_root_node_;

  fs::SynchronousVfs manifest_fs_;
  fbl::RefPtr<fs::PseudoDir> manifest_fs_root_node_;

  std::unique_ptr<fsl::DeviceWatcher> gpu_watcher_;
  std::unique_ptr<fsl::DeviceWatcher> goldfish_watcher_;

  std::vector<std::unique_ptr<GpuDevice>> devices_;

  std::unordered_map<std::string, std::shared_ptr<IcdComponent>> icd_components_;

  fxl::ObserverList<Observer> observer_list_;

  // The FDIO loop is used to run FDIO commands that may access an ICD
  // component's package. Those commands may block because they require the
  // IcdRunner to service them.
  async::Loop fdio_loop_;
};

#endif  // SRC_GRAPHICS_BIN_VULKAN_LOADER_APP_H_
