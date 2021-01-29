// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_SUSPEND_HANDLER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_SUSPEND_HANDLER_H_

#include <fuchsia/fshost/llcpp/fidl.h>

#include "suspend_task.h"

using SuspendCallback = fit::callback<void(zx_status_t)>;

class SuspendHandler {
 public:
  enum class Flags : uint32_t {
    kRunning = 0u,
    kSuspend = 1u,
  };

  // Create a SuspendHandler. `coordinator` is a weak pointer that must outlive `SuspendHandler`.
  SuspendHandler(Coordinator* coordinator, bool suspend_fallback, zx::duration suspend_timeout);

  bool InSuspend() const { return flags_ == SuspendHandler::Flags::kSuspend; }

  void Suspend(uint32_t flags, SuspendCallback callback);

  // Shut down all filesystems (and fshost itself) by calling
  // fuchsia.fshost.Admin.Shutdown(). Note that this is called from multiple
  // different locations; during suspension, and in a low-memory situation.
  // Currently, both of these calls happen on the same dispatcher thread, but
  // consider thread safety when refactoring.
  void ShutdownFilesystems(fit::callback<void(zx_status_t)> callback);

  // For testing only: Set the fshost admin client.
  void set_fshost_admin_client(fidl::Client<llcpp::fuchsia::fshost::Admin> client) {
    fshost_admin_client_ = std::move(client);
  }

  const SuspendTask* task() const { return task_.get(); }
  Flags flags() const { return flags_; }
  uint32_t sflags() const { return sflags_; }

 private:
  void SuspendAfterFilesystemShutdown();

  Coordinator* coordinator_;
  bool suspend_fallback_;
  zx::duration suspend_timeout_;

  SuspendCallback suspend_callback_;
  fbl::RefPtr<SuspendTask> task_;
  std::unique_ptr<async::TaskClosure> suspend_watchdog_task_;
  fidl::Client<llcpp::fuchsia::fshost::Admin> fshost_admin_client_;

  Flags flags_ = Flags::kRunning;

  // suspend flags
  uint32_t sflags_ = 0u;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_SUSPEND_HANDLER_H_
