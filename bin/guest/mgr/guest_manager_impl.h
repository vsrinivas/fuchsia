// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_MGR_GUEST_MANAGER_IMPL_H_
#define GARNET_BIN_GUEST_MGR_GUEST_MANAGER_IMPL_H_

#include <fuchsia/guest/cpp/fidl.h>

#include <unordered_map>

#include "garnet/bin/guest/mgr/guest_environment_impl.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"

namespace guestmgr {

class EnvironmentManagerImpl : public fuchsia::guest::EnvironmentManager {
 public:
  EnvironmentManagerImpl();
  ~EnvironmentManagerImpl() override;

 private:
  // |fuchsia::guest::EnvironmentManager|
  void Create(
      fidl::StringPtr label,
      fidl::InterfaceRequest<fuchsia::guest::EnvironmentController> env) override;
  void List(ListCallback callback) override;
  void Connect(
      uint32_t id,
      fidl::InterfaceRequest<fuchsia::guest::EnvironmentController> env) override;

  std::unique_ptr<component::StartupContext> context_;
  fidl::BindingSet<fuchsia::guest::EnvironmentManager> bindings_;
  std::unordered_map<uint32_t, std::unique_ptr<EnvironmentControllerImpl>>
      environments_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EnvironmentManagerImpl);
};

}  // namespace guestmgr

#endif  // GARNET_BIN_GUEST_MGR_GUEST_MANAGER_IMPL_H_
