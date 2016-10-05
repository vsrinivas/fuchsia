// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of a dummy User shell.
// This takes |recipe_url| as a command line argument and passes it to the
// Story Manager.

#include <mojo/system/main.h>

#include "apps/modular/story_manager/story_manager.mojom.h"
#include "apps/modular/mojo/single_service_application.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/synchronization/sleep.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/interface_ptr_set.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace modular {

constexpr char kExampleRecipeUrl[] = "mojo:example_recipe";

using mojo::ApplicationImplBase;
using mojo::ConnectionContext;
using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::ServiceProviderImpl;
using mojo::StrongBinding;
using mojo::StructPtr;

class DummyUserShellImpl : public UserShell {
 public:
  explicit DummyUserShellImpl(InterfaceRequest<UserShell> request)
      : binding_(this, std::move(request)) {}
  ~DummyUserShellImpl() override{};

  void SetStoryProvider(
      InterfaceHandle<StoryProvider> story_provider) override {
    story_provider_.Bind(story_provider.Pass());

    // Check for previous stories.
    story_provider_->PreviousStories([this](InterfaceHandle<Story> story) {
      FTL_DCHECK(!story.is_valid());
    });

    // Start a new story.
    story_provider_->StartNewStory(
        kExampleRecipeUrl, [this](InterfaceHandle<Story> story) {
          FTL_LOG(INFO) << "Received modular::Story from provider.";
          story_ptr_.Bind(story.Pass());
          story_ptr_->GetInfo([this](StructPtr<StoryInfo> story_info) {
            FTL_LOG(INFO) << "modular::Story received with url: "
                          << story_info->url
                          << " is_running: " << story_info->is_running;

            // Let the story run for 2500 milli-seconds before stopping.
            ftl::SleepFor(ftl::TimeDelta::FromMilliseconds(2500));

            story_ptr_->Stop();

            // Resume the stopped story.
            story_ptr_->Resume();
          });
        });
  }

 private:
  StrongBinding<UserShell> binding_;
  InterfacePtr<StoryProvider> story_provider_;
  InterfacePtr<Story> story_ptr_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DummyUserShellImpl);
};

}  // namespace modular

MojoResult MojoMain(MojoHandle application_request) {
  FTL_LOG(INFO) << "dummy_user_shell main";
  modular::SingleServiceApplication<modular::UserShell, modular::DummyUserShellImpl> app;
  return mojo::RunApplication(application_request, &app);
}
