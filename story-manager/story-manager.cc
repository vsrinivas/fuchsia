// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the story manager mojo app and of all mojo
// services it provides directly or transitively from other services.
// The mojom definitions for the services are in
// ../mojom_hack/story_manager.mojom, though they should be here.

#include <mojo/system/main.h>

#include "apps/modular/mojom_hack/story_manager.mojom.h"
#include "apps/modular/mojom_hack/story_runner.mojom.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace story_manager {

class StoryProviderImpl : public StoryProvider {
 public:
  explicit StoryProviderImpl(mojo::Shell* shell,
                             mojo::InterfaceHandle<StoryProvider>* service)
      : shell_(shell), binding_(this) {
    binding_.Bind(mojo::GetProxy(service));
  }

  ~StoryProviderImpl() override {}

 private:
  void StartNewStory(const mojo::String& url,
                     const StartNewStoryCallback& callback) override {
    FTL_LOG(INFO) << "Received request for starting application at " << url;
    // TODO(alhaad): Creating multiple stories can only work after
    // https://fuchsia-review.googlesource.com/#/c/8941/ has landed.
    auto new_session_id = std::to_string(session_map_.size());
    mojo::InterfacePtr<story::Runner> runner;
    mojo::InterfacePtr<story::Session> session;
    mojo::InterfacePtr<story::Module> module;

    mojo::InterfacePtr<mojo::ServiceProvider> service_provider;
    shell_->ConnectToApplication("mojo:story-runner",
                                 mojo::GetProxy(&service_provider));
    service_provider->ConnectToService(
        story::Runner::Name_, mojo::GetProxy(&runner).PassMessagePipe());

    runner->StartStory(GetProxy(&session));
    mojo::InterfaceHandle<story::Link> link;
    session->CreateLink("boot", GetProxy(&link));
    session->StartModule(
        url, std::move(link),
        [this, new_session_id](mojo::InterfaceHandle<story::Module> m) {
          std::get<2>(session_map_[new_session_id]).Bind(std::move(m));
        });

    auto tuple = std::make_tuple(std::move(runner), std::move(session),
                                 std::move(module));
    session_map_.emplace(std::move(new_session_id), std::move(tuple));
    callback.Run(nullptr);
  }

  mojo::Shell* shell_;
  mojo::StrongBinding<StoryProvider> binding_;

  // A |SessionMap| stores a list of all session IDs, mapping them to the
  // corresponding Runner, Session and (root) Module (which is the recipe).
  using SessionMap =
      std::map<std::string, std::tuple<mojo::InterfacePtr<story::Runner>,
                                       mojo::InterfacePtr<story::Session>,
                                       mojo::InterfacePtr<story::Module>>>;
  SessionMap session_map_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryProviderImpl);
};

class StoryManagerImpl : public StoryManager {
 public:
  explicit StoryManagerImpl(mojo::Shell* shell,
                            mojo::InterfaceRequest<StoryManager> request)
      : shell_(shell), binding_(this, std::move(request)) {}
  ~StoryManagerImpl() override {}

 private:
  void Launch(mojo::StructPtr<ledger::Identity> identity,
              const LaunchCallback& callback) override {
    FTL_LOG(INFO) << "story_manager::Launch received.";

    // Establish connection with Ledger.
    mojo::InterfacePtr<mojo::ServiceProvider> service_provider;
    shell_->ConnectToApplication("mojo:ledger_abax",
                                 mojo::GetProxy(&service_provider));
    service_provider->ConnectToService(
        ledger::LedgerFactory::Name_,
        mojo::GetProxy(&ledger_factory_).PassMessagePipe());
    ledger_factory_->GetLedger(
        std::move(identity),
        [this](ledger::Status s, mojo::InterfaceHandle<ledger::Ledger> l) {
          if (s == ledger::Status::OK) {
            FTL_LOG(INFO) << "story-manager successfully connected to ledger.";
          } else {
            FTL_LOG(ERROR) << "story-manager's connection to ledger failed.";
          }
        });

    // TODO(alhaad): Everything below this line should happen only after a
    // successful Ledger connection. Figure it out when dealing with Ledger
    // integration.
    StartUserShell();
    callback.Run(true);
  }

  // Run the User shell and provide it the |StoryProvider| interface.
  void StartUserShell() {
    mojo::InterfacePtr<mojo::ServiceProvider> service_provider;
    shell_->ConnectToApplication("mojo:dummy-user-shell",
                                 mojo::GetProxy(&service_provider));
    service_provider->ConnectToService(
        UserShell::Name_, mojo::GetProxy(&user_shell_).PassMessagePipe());
    mojo::InterfaceHandle<StoryProvider> service;
    new StoryProviderImpl(shell_, &service);
    user_shell_->SetStoryProvider(std::move(service));
  }

  mojo::Shell* shell_;
  mojo::StrongBinding<StoryManager> binding_;

  mojo::InterfacePtr<UserShell> user_shell_;

  mojo::InterfacePtr<ledger::LedgerFactory> ledger_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryManagerImpl);
};

class StoryManagerApp : public mojo::ApplicationImplBase {
 public:
  StoryManagerApp() {}
  ~StoryManagerApp() override {}

 private:
  void OnInitialize() override { FTL_LOG(INFO) << "story-manager init"; }

  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override {
    // Register |StoryManager| implementation.
    service_provider_impl->AddService<StoryManager>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<StoryManager> launcher_request) {
          new StoryManagerImpl(shell(), std::move(launcher_request));
        });
    return true;
  }

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryManagerApp);
};

}  // namespace story_manager

MojoResult MojoMain(MojoHandle application_request) {
  story_manager::StoryManagerApp app;
  return mojo::RunApplication(application_request, &app);
}
