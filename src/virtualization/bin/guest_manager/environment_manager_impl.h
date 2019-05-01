// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_ENVIRONMENT_MANAGER_IMPL_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_ENVIRONMENT_MANAGER_IMPL_H_

#include <fuchsia/guest/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>

#include <unordered_map>

#include "src/virtualization/bin/guest_manager/environment_controller_impl.h"

class EnvironmentManagerImpl : public fuchsia::guest::EnvironmentManager {
 public:
  EnvironmentManagerImpl();

  EnvironmentManagerImpl(const EnvironmentManagerImpl&) = delete;
  EnvironmentManagerImpl& operator=(const EnvironmentManagerImpl&) = delete;

 private:
  // |fuchsia::guest::EnvironmentManager|
  void Create(fidl::StringPtr label,
              fidl::InterfaceRequest<fuchsia::guest::EnvironmentController> env)
      override;
  void List(ListCallback callback) override;
  void Connect(uint32_t id,
               fidl::InterfaceRequest<fuchsia::guest::EnvironmentController>
                   env) override;

  std::unique_ptr<component::StartupContext> context_;
  std::unordered_map<uint32_t, std::unique_ptr<EnvironmentControllerImpl>>
      environments_;
  fidl::BindingSet<fuchsia::guest::EnvironmentManager> bindings_;
};

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_ENVIRONMENT_MANAGER_IMPL_H_
