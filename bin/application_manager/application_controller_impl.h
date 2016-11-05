// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_APPLICATION_MANAGER_APPLICATION_CONTROLLER_IMPL_H_
#define APPS_MODULAR_SRC_APPLICATION_MANAGER_APPLICATION_CONTROLLER_IMPL_H_

#include <mx/process.h>

#include "lib/ftl/macros.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "apps/modular/services/application/application_controller.fidl.h"

namespace modular {
class ApplicationEnvironmentImpl;

class ApplicationControllerImpl : public ApplicationController {
 public:
  ApplicationControllerImpl(
      fidl::InterfaceRequest<ApplicationController> request,
      ApplicationEnvironmentImpl* environment,
      mx::process process);
  ~ApplicationControllerImpl() override;

  void Kill(const KillCallback& callback) override;

  void Detach() override;

 private:
  fidl::Binding<ApplicationController> binding_;
  ApplicationEnvironmentImpl* environment_;
  mx::process process_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ApplicationControllerImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_APPLICATION_MANAGER_APPLICATION_CONTROLLER_IMPL_H_
