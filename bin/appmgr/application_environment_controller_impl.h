// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_SRC_MANAGER_APPLICATION_ENVIRONMENT_CONTROLLER_IMPL_H_
#define APPLICATION_SRC_MANAGER_APPLICATION_ENVIRONMENT_CONTROLLER_IMPL_H_

#include <memory>

#include "lib/app/fidl/application_environment_controller.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"

namespace app {
class ApplicationEnvironmentImpl;

class ApplicationEnvironmentControllerImpl
    : public ApplicationEnvironmentController {
 public:
  ApplicationEnvironmentControllerImpl(
      fidl::InterfaceRequest<ApplicationEnvironmentController> request,
      std::unique_ptr<ApplicationEnvironmentImpl> environment);
  ~ApplicationEnvironmentControllerImpl() override;

  ApplicationEnvironmentImpl* environment() const { return environment_.get(); }

  // ApplicationEnvironmentController implementation:

  void Kill(const KillCallback& callback) override;

  void Detach() override;

 private:
  fidl::Binding<ApplicationEnvironmentController> binding_;
  std::unique_ptr<ApplicationEnvironmentImpl> environment_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ApplicationEnvironmentControllerImpl);
};

}  // namespace app

#endif  // APPLICATION_SRC_MANAGER_APPLICATION_ENVIRONMENT_CONTROLLER_IMPL_H_
