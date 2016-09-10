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

namespace story_manager {

class StoryManagerApp : public mojo::ApplicationImplBase,
                        public Launcher,
                        public StoryProvider {
 public:
  StoryManagerApp() : launcher_binding_(this), story_provider_binding_(this) {}
  ~StoryManagerApp() override {}

 private:
  // |ApplicationImplBase| override
  void OnInitialize() override { FTL_LOG(INFO) << "story-manager init"; }

  // |ApplicationImplBase| override
  // This is meant to be called only once from device-runner.
  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override {
    // Register |Launcher| implementation.
    service_provider_impl->AddService<Launcher>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<Launcher> launcher_request) {
          launcher_binding_.Bind(launcher_request.Pass());
        });

    // Register |StoryProvider| implementation.
    // HACK(alhaad): Eventually |StoryProvider| will not be exposed here but via
    // a different service provider to SysUI.
    service_provider_impl->AddService<StoryProvider>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<StoryProvider> story_provider_request) {
          story_provider_binding_.Bind(story_provider_request.Pass());
        });
    return true;
  }

  // |Launcher| override.
  void Launch(mojo::StructPtr<ledger::Identity> identity,
              const LaunchCallback& callback) override {
    FTL_LOG(INFO) << "story_manager::Launch received.";
    mojo::ConnectToService(shell(), "mojo:ledger_abax",
                           GetProxy(&ledger_factory_));
    ledger_factory_->GetLedger(
        std::move(identity),
        [this](ledger::Status s, mojo::InterfaceHandle<ledger::Ledger> l) {
          if (s == ledger::Status::OK) {
            FTL_LOG(INFO) << "story-manager successfully connected to ledger.";
          } else {
            FTL_LOG(ERROR) << "story-manager's connection to ledger failed.";
          }
        });
    callback.Run(true);
  }

  // |StoryProvider| override.
  void StartNewStory(const mojo::String& url,
                     const StartNewStoryCallback& callback) override {
    FTL_LOG(INFO) << "Received request for starting application at " << url;
    // TODO(alhaad): Creating multiple stories can only work after
    // https://fuchsia-review.googlesource.com/#/c/8941/ has landed.
    auto new_session_id = std::to_string(session_map_.size());
    mojo::InterfacePtr<story::Runner> runner;
    mojo::InterfacePtr<story::Session> session;
    mojo::InterfacePtr<story::Module> module;

    ConnectToService(shell(), "mojo:story-runner", GetProxy(&runner));
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

  mojo::Binding<Launcher> launcher_binding_;
  mojo::Binding<StoryProvider> story_provider_binding_;

  mojo::InterfacePtr<ledger::LedgerFactory> ledger_factory_;

  // A |SessionMap| stores a list of all session IDs, mapping them to the
  // corresponding Runner, Session and (root) Module (which is the recipe).
  using SessionMap =
      std::map<std::string, std::tuple<mojo::InterfacePtr<story::Runner>,
                                       mojo::InterfacePtr<story::Session>,
                                       mojo::InterfacePtr<story::Module>>>;
  SessionMap session_map_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryManagerApp);
};

}  // namespace story_manager

MojoResult MojoMain(MojoHandle application_request) {
  story_manager::StoryManagerApp story_manager_app;
  return mojo::RunApplication(application_request, &story_manager_app);
}
