// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_GUEST_MANAGER_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_GUEST_MANAGER_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/status.h>

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
  void ConnectToBalloon(
      fidl::InterfaceRequest<fuchsia::virtualization::BalloonController> controller) override;

  void GetGuestInfo(GetGuestInfoCallback callback) override;

  // |fuchsia::virtualization::GuestConfigProvider|
  void Get(GetCallback callback) override;

  bool is_guest_started() const { return guest_started_; }

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
  bool guest_started_ = false;
};

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_GUEST_MANAGER_H_
