// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_APPLICATION_ENVIRONMENT_CONTROLLER_IMPL_H_
#define GARNET_BIN_APPMGR_APPLICATION_ENVIRONMENT_CONTROLLER_IMPL_H_

#include <memory>

#include <component/cpp/fidl.h>
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"

namespace component {
class Realm;

class ApplicationEnvironmentControllerImpl
    : public ApplicationEnvironmentController {
 public:
  ApplicationEnvironmentControllerImpl(
      fidl::InterfaceRequest<ApplicationEnvironmentController> request,
      std::unique_ptr<Realm> realm);
  ~ApplicationEnvironmentControllerImpl() override;

  Realm* realm() const { return realm_.get(); }

  // ApplicationEnvironmentController implementation:

  void Kill(KillCallback callback) override;

  void Detach() override;

 private:
  fidl::Binding<ApplicationEnvironmentController> binding_;
  std::unique_ptr<Realm> realm_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ApplicationEnvironmentControllerImpl);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_APPLICATION_ENVIRONMENT_CONTROLLER_IMPL_H_
