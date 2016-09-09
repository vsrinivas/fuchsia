// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>
#include <stdio.h>
#include <vector>

#include "apps/modular/mojom_hack/story_runner.mojom.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/environment/logging.h"
#include "mojo/public/cpp/system/macros.h"
#include "mojo/public/interfaces/application/service_provider.mojom.h"
#include "mojo/public/interfaces/application/shell.mojom.h"

namespace {

using mojo::ApplicationImplBase;
using mojo::Binding;
using mojo::BindingSet;
using mojo::ConnectToService;
using mojo::ConnectionContext;
using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::ServiceProvider;
using mojo::ServiceProviderImpl;
using mojo::Shell;
using mojo::StrongBinding;

using story::Link;
using story::Module;
using story::Runner;
using story::Session;

// A simple mojo app that runs the story runner app and starts a dummy
// example story. This is used to be able to run a story from the
// fuchsia command line using application manager. To be replaced by
// invoking story runner from story manager.
class BootApp : public ApplicationImplBase {
 public:
  BootApp() {}
  ~BootApp() override {}

  void OnInitialize() override {
    FTL_LOG(INFO) << "story-runner-boot init";

    ConnectToService(shell(), "mojo:story-runner", GetProxy(&runner_));
    runner_->StartStory(GetProxy(&session_));

    InterfaceHandle<Link> link;
    session_->CreateLink("boot", GetProxy(&link));

    session_->StartModule("mojo:example-recipe", std::move(link),
                          [this](InterfaceHandle<Module> module) {
                            module_.Bind(std::move(module));
                          });

    FTL_LOG(INFO) << "story-runner-boot init done";
  }

 private:
  InterfacePtr<Runner> runner_;
  InterfacePtr<Session> session_;
  InterfacePtr<Module> module_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(BootApp);
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  FTL_LOG(INFO) << "story-runner-boot main";
  BootApp app;
  return mojo::RunApplication(request, &app);
}
