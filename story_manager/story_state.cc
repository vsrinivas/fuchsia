// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/story_manager/story_state.h"

#include "apps/modular/story_manager/story_provider_state.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/connect.h"

namespace modular {

StoryState::StoryState(
    mojo::StructPtr<StoryInfo> story_info,
    StoryProviderState* story_provider_state,
    mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
    mojo::InterfaceRequest<Story> request)
    : story_info_(std::move(story_info)),
      story_provider_state_(story_provider_state),
      app_connector_(mojo::InterfacePtr<mojo::ApplicationConnector>::Create(
          std::move(app_connector))),
      binding_(this, std::move(request)),
      module_watcher_binding_(this) {
  FTL_LOG(INFO) << "StoryState()";
}

StoryState::~StoryState() {
  FTL_LOG(INFO) << "~StoryState()";
  story_provider_state_->CommitStoryState(this);
  story_provider_state_->RemoveStoryState(this);
}

mojo::StructPtr<StoryInfo> StoryState::GetStoryInfo() const {
  return story_info_->Clone();
}

void StoryState::RunStory(
    mojo::InterfacePtr<ledger::Page> session_page,
    mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  FTL_LOG(INFO) << "StoryState::RunStory()";
  mojo::InterfacePtr<ResolverFactory> resolver_factory;
  mojo::ConnectToService(app_connector_.get(), "mojo:resolver",
                         GetProxy(&resolver_factory));
  mojo::ConnectToService(app_connector_.get(), "mojo:story_runner",
                         GetProxy(&runner_));
  runner_->Initialize(std::move(resolver_factory));
  runner_->StartStory(session_page.PassInterfaceHandle(), GetProxy(&session_));
  mojo::InterfaceHandle<Link> link;
  session_->CreateLink(GetProxy(&link));
  session_->StartModule(story_info_->url, std::move(link), GetProxy(&module_),
                        std::move(view_owner_request));
  story_info_->is_running = true;

  mojo::InterfaceHandle<ModuleWatcher> module_watcher;
  module_watcher_binding_.Bind(GetProxy(&module_watcher));
  module_->Watch(std::move(module_watcher));
}

void StoryState::Done() {
  FTL_LOG(INFO) << "StoryState::Done()";
  Stop();

  // Deleting |this| causes |Story| interface to be closed which is an
  // indication for UserShell that this story has terminated.
  delete this;
}

void StoryState::GetInfo(const GetInfoCallback& callback) {
  callback.Run(story_info_->Clone());
}

void StoryState::Stop() {
  FTL_LOG(INFO) << "StoryState::Stop()";
  if (!story_info_->is_running) {
    return;
  }

  module_.reset();
  session_.reset();
  runner_.reset();
  module_watcher_binding_.Close();
  story_provider_state_->CommitStoryState(this);
  story_info_->is_running = false;
}

void StoryState::Start(
    mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  FTL_LOG(INFO) << "StoryState::Resume()";
  if (story_info_->is_running) {
    return;
  }

  story_provider_state_->ResumeStoryState(this, std::move(view_owner_request));
}

}  // namespace modular
