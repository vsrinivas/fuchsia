// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_GUEST_MANAGER_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_GUEST_MANAGER_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/status.h>
#include <zircon/types.h>

#include "src/virtualization/lib/guest_config/guest_config.h"

class GuestManager : public fuchsia::virtualization::GuestManager,
                     public fuchsia::virtualization::GuestConfigProvider {
 public:
  GuestManager(async_dispatcher_t* dispatcher, sys::ComponentContext* context,
               std::string config_pkg_dir_path, std::string config_path);

  GuestManager(async_dispatcher_t* dispatcher, sys::ComponentContext* context)
      : GuestManager(dispatcher, context, "/guest_pkg/", "data/guest.cfg") {}

  // |fuchsia::virtualization::GuestManager|
  void LaunchGuest(fuchsia::virtualization::GuestConfig guest_config,
                   fidl::InterfaceRequest<fuchsia::virtualization::Guest> controller,
                   fuchsia::virtualization::GuestManager::LaunchGuestCallback callback) override;
  void ConnectToGuest(fidl::InterfaceRequest<fuchsia::virtualization::Guest> controller,
                      fuchsia::virtualization::GuestManager::ConnectToGuestCallback) override;
  void GetGuestInfo(GetGuestInfoCallback callback) override;

  // |fuchsia::virtualization::GuestConfigProvider|
  void Get(GetCallback callback) override;

  void SnapshotConfig(const fuchsia::virtualization::GuestConfig& config);

  bool is_guest_started() const {
    return state_ != fuchsia::virtualization::GuestStatus::NOT_STARTED;
  }

 protected:
  virtual zx::status<fuchsia::virtualization::GuestConfig> GetDefaultGuestConfig();
  virtual void OnGuestLaunched() {}

 private:
  sys::ComponentContext* context_;
  fidl::BindingSet<fuchsia::virtualization::GuestManager> manager_bindings_;
  fidl::BindingSet<fuchsia::virtualization::GuestConfigProvider> guest_config_bindings_;
  fuchsia::virtualization::GuestConfig guest_config_;
  std::string config_pkg_dir_path_;
  std::string config_path_;

  // Cached error reported by the VMM upon stopping if not stopped due to a clean shutdown.
  std::optional<fuchsia::virtualization::GuestError> last_error_;

  // Used to calculate the guest's uptime for guest info reporting.
  zx::time start_time_ = zx::time::infinite_past();
  zx::time stop_time_ = zx::time::infinite_past();

  // Snapshot of some of the configuration settings used to start this guest. This is
  // informational only, and sent in response to a GetGuestInfo call.
  fuchsia::virtualization::GuestDescriptor guest_descriptor_;

  // Current state of the guest.
  fuchsia::virtualization::GuestStatus state_ = fuchsia::virtualization::GuestStatus::NOT_STARTED;
};

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_GUEST_MANAGER_H_
