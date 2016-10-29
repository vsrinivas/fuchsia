// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the story runner mojo app and of all mojo
// services it provides directly or transitively from other services.

#include <mojo/system/main.h>

#include "apps/modular/services/story/session.mojom.h"
#include "apps/modular/services/story/story_runner.mojom.h"
#include "apps/modular/story_runner/session.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/system/macros.h"
#include "mojo/public/interfaces/application/shell.mojom.h"

namespace modular {

// The story runner service is the service directly provided by the
// story runner app. It must be initialized with a resolver factory
// and then allows to create a Session.
class StoryRunnerImpl : public StoryRunner {
 public:
  StoryRunnerImpl(mojo::Shell* const shell,
                  mojo::InterfaceRequest<StoryRunner> req)
      : shell_(shell), binding_(this, std::move(req)) {
    FTL_LOG(INFO) << "StoryRunnerImpl()";
  }

  ~StoryRunnerImpl() override { FTL_LOG(INFO) << "~StoryRunnerImpl()"; }

  void Initialize(
      mojo::InterfaceHandle<ResolverFactory> resolver_factory) override {
    resolver_factory_.Bind(std::move(resolver_factory));
  }

  void StartStory(mojo::InterfaceHandle<ledger::Page> session_page,
                  mojo::InterfaceRequest<Session> session) override {
    mojo::InterfaceHandle<Resolver> resolver;
    resolver_factory_->GetResolver(GetProxy(&resolver));
    new SessionImpl(shell_, std::move(resolver), std::move(session_page),
                    std::move(session));
  }

 private:
  mojo::Shell* const shell_;
  mojo::InterfacePtr<ResolverFactory> resolver_factory_;
  mojo::StrongBinding<StoryRunner> binding_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(StoryRunnerImpl);
};

// The StoryRunner mojo app provides instances of the implementation
// of the StoryRunner service. It is a single service app, but the
// service impl takes the shell as additional constructor parameter,
// so we don't reuse the template class here.
class StoryRunnerApp : public mojo::ApplicationImplBase {
 public:
  StoryRunnerApp() {}
  ~StoryRunnerApp() override {}

  bool OnAcceptConnection(mojo::ServiceProviderImpl* const s) override {
    s->AddService<StoryRunner>([this](const mojo::ConnectionContext& ctx,
                                      mojo::InterfaceRequest<StoryRunner> req) {
      new StoryRunnerImpl(shell(), std::move(req));
    });

    return true;
  }

 private:
  MOJO_DISALLOW_COPY_AND_ASSIGN(StoryRunnerApp);
};

}  // namespace modular

MojoResult MojoMain(MojoHandle request) {
  FTL_LOG(INFO) << "story-runner main";
  modular::StoryRunnerApp app;
  return mojo::RunApplication(request, &app);
}
