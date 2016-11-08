// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the story runner application and of all services
// that it provides directly or transitively through other services.

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/mojo/strong_binding.h"
#include "apps/modular/services/application/application_launcher.fidl.h"
#include "apps/modular/services/story/story.fidl.h"
#include "apps/modular/services/story/story_runner.fidl.h"
#include "apps/modular/src/story_runner/story_impl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

// The story runner service is the service directly provided by the
// story runner app. It must be initialized with a resolver factory
// and then allows to create a Story.
class StoryRunnerImpl : public StoryRunner {
 public:
  StoryRunnerImpl(std::shared_ptr<ApplicationContext> application_context,
                  fidl::InterfaceRequest<StoryRunner> req)
      : application_context_(application_context),
        binding_(this, std::move(req)) {
    FTL_LOG(INFO) << "StoryRunnerImpl()";
  }

  ~StoryRunnerImpl() override { FTL_LOG(INFO) << "~StoryRunnerImpl()"; }

  void Initialize(
      fidl::InterfaceHandle<ResolverFactory> resolver_factory) override {
    resolver_factory_.Bind(std::move(resolver_factory));
  }

  void StartStory(fidl::InterfaceHandle<StoryStorage> story_storage,
                  fidl::InterfaceRequest<Story> story) override {
    fidl::InterfaceHandle<Resolver> resolver;
    resolver_factory_->GetResolver(GetProxy(&resolver));
    new StoryImpl(application_context_, std::move(resolver),
                  std::move(story_storage), std::move(story));
  }

 private:
  std::shared_ptr<ApplicationContext> application_context_;
  fidl::InterfacePtr<ResolverFactory> resolver_factory_;
  StrongBinding<StoryRunner> binding_;
  FTL_DISALLOW_COPY_AND_ASSIGN(StoryRunnerImpl);
};

// The StoryRunnerApp provides instances of the implementation of the
// StoryRunner service. It is a single service app, but the service
// impl takes the application launcher as additional constructor
// parameter, so we don't reuse the template class here.
class StoryRunnerApp {
 public:
  StoryRunnerApp() : context_(ApplicationContext::CreateFromStartupInfo()) {
    FTL_LOG(INFO) << "StoryRunnerApp()";
    context_->outgoing_services()->AddService<StoryRunner>(
        [this](fidl::InterfaceRequest<StoryRunner> request) {
          new StoryRunnerImpl(context_, std::move(request));
        });
  }

 private:
  std::shared_ptr<ApplicationContext> context_;
  FTL_DISALLOW_COPY_AND_ASSIGN(StoryRunnerApp);
};

}  // namespace modular

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  modular::StoryRunnerApp app;
  loop.Run();
  return 0;
}
