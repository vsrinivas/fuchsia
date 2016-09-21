// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A common base class for all the Module apps in this directory.

#ifndef APPS_MODULAR_STORY_EXAMPLE_MODULE_APP_H_
#define APPS_MODULAR_STORY_EXAMPLE_MODULE_APP_H_

#include "apps/modular/story_runner/story_runner.mojom.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/service_provider_impl.h"

namespace story {

template <class ModuleImpl>
class ModuleApp : public mojo::ApplicationImplBase {
 public:
  ModuleApp() {}
  ~ModuleApp() override {}

  bool OnAcceptConnection(mojo::ServiceProviderImpl* const s) override {
    s->AddService<Module>(
        [](const mojo::ConnectionContext& ctx,
           mojo::InterfaceRequest<Module> req) {
          new ModuleImpl(std::move(req));
        });
    return true;
  }

 private:
  MOJO_DISALLOW_COPY_AND_ASSIGN(ModuleApp);
};

}  // namespace story

#endif  // APPS_MODULAR_STORY_EXAMPLE_MODULE_APP_H_
