// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of a dummy User shell.
// This takes |recipe_url| as a command line argument and passes it to the
// Story Manager.

#include <mojo/system/main.h>

#include "apps/modular/mojom_hack/story_manager.mojom.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/interface_ptr_set.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace {

class DummyUserShellImpl : public story_manager::UserShell {
 public:
  explicit DummyUserShellImpl(
      const std::string& recipe_url,
      mojo::InterfaceRequest<story_manager::UserShell> request)
      : binding_(this, std::move(request)), recipe_url_(recipe_url) {}
  ~DummyUserShellImpl() override {};

 private:
  void SetStoryProvider(mojo::InterfaceHandle<story_manager::StoryProvider>
                            story_provider) override {
    story_provider_ = mojo::InterfacePtr<story_manager::StoryProvider>::Create(
        story_provider.Pass());
    story_provider_->StartNewStory(
        std::move(recipe_url_),
        [this](mojo::InterfaceHandle<story_manager::Story> story) {
      FTL_LOG(INFO) << "Received story_manager::Story from provider.";
      mojo::InterfacePtr<story_manager::Story> story_ptr =
          mojo::InterfacePtr<story_manager::Story>::Create(story.Pass());
      story_ptr->GetMetadata(
          [](mojo::InlinedStructPtr<story_manager::StoryMetadata>
                  story_metadata) {
          FTL_LOG(INFO) << "story_manager::Story received with url: "
                        << story_metadata->url << " is_running: "
                        << story_metadata->is_running;
      });
      story_ptr_set_.AddInterfacePtr(story_ptr.Pass());
    });
  }

  mojo::InterfacePtr<story_manager::StoryProvider> story_provider_;
  mojo::InterfacePtrSet<story_manager::Story> story_ptr_set_;

  mojo::StrongBinding<story_manager::UserShell> binding_;
  std::string recipe_url_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DummyUserShellImpl);
};

class DummyUserShellApp : public mojo::ApplicationImplBase {
 public:
  DummyUserShellApp() {}
  ~DummyUserShellApp() override {}

 private:
  void OnInitialize() override {
    if (args().size() != 1) {
      FTL_DLOG(INFO) << "dummy-user-runner expects 1 additional argument.\n"
                     << "Usage: mojo:dummy-user-runner [recipe]";
      return;
    }

    FTL_LOG(INFO) << "dummy-user_shell init";
    recipe_url_ = args()[0];
  }

  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override {
    // Register |UserShell| implementation.
    service_provider_impl->AddService<story_manager::UserShell>([this](
        const mojo::ConnectionContext& connection_context,
        mojo::InterfaceRequest<story_manager::UserShell> user_shell_request) {
      new DummyUserShellImpl(recipe_url_, std::move(user_shell_request));
    });
    return true;
  }

  std::string recipe_url_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DummyUserShellApp);
};

}  // namespace

MojoResult MojoMain(MojoHandle application_request) {
  DummyUserShellApp app;
  return mojo::RunApplication(application_request, &app);
}
