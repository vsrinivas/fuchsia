// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULE_CPP_MODULE_IMPL_H_
#define LIB_MODULE_CPP_MODULE_IMPL_H_

#include <memory>

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fxl/macros.h"
#include "lib/module/fidl/module.fidl.h"
#include "lib/module/fidl/module_context.fidl.h"
#include "lib/svc/cpp/service_namespace.h"

namespace modular {

// Use this class to talk to the modular framework as a Module.
class ModuleImpl : public Module {
 public:
  // Users of ModuleImpl register a delegate to receive initialization
  // parameters.
  class Delegate {
   public:
    virtual void ModuleInit(
        fidl::InterfaceHandle<ModuleContext> module_context,
        fidl::InterfaceRequest<app::ServiceProvider> outgoing_services) = 0;
  };

  ModuleImpl(app::ServiceNamespace* service_namespace, Delegate* delegate);

 private:
  // |Module|
  void Initialize(
      fidl::InterfaceHandle<ModuleContext> module_context,
      fidl::InterfaceRequest<app::ServiceProvider> outgoing_services) override;

  Delegate* const delegate_;
  fidl::Binding<Module> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleImpl);
};

}  // namespace modular

#endif  // LIB_MODULE_CPP_MODULE_IMPL_H_
