// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/story_manager/story_impl.h"

#include "apps/modular/story_manager/story_provider_impl.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/connect.h"

namespace modular {

StoryImpl::StoryImpl(
    mojo::StructPtr<StoryInfo> story_info,
    StoryProviderImpl* const story_provider_impl,
    mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
    mojo::InterfaceRequest<Story> story_request)
    : story_info_(std::move(story_info)),
      story_provider_impl_(story_provider_impl),
      binding_(this, std::move(story_request)),
      module_watcher_binding_(this) {
  FTL_LOG(INFO) << "StoryImpl()";
  app_connector_.Bind(std::move(app_connector));
}

StoryImpl::~StoryImpl() {
  FTL_LOG(INFO) << "~StoryImpl()";
  story_provider_impl_->CommitStory(this);
  story_provider_impl_->RemoveStory(this);
}

mojo::StructPtr<StoryInfo> StoryImpl::GetStoryInfo() const {
  return story_info_->Clone();
}

void StoryImpl::RunStory(
    mojo::InterfacePtr<ledger::Page> session_page,
    mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  FTL_LOG(INFO) << "StoryImpl::RunStory()";
  mojo::InterfacePtr<ResolverFactory> resolver_factory;
  mojo::ConnectToService(app_connector_.get(), "mojo:resolver",
                         GetProxy(&resolver_factory));
  mojo::ConnectToService(app_connector_.get(), "mojo:story_runner",
                         GetProxy(&runner_));

  runner_->Initialize(std::move(resolver_factory));
  runner_->StartStory(std::move(session_page), GetProxy(&session_));

  mojo::InterfaceHandle<Link> link;
  session_->CreateLink("root", GetProxy(&link));
  session_->StartModule(story_info_->url, std::move(link), GetProxy(&module_),
                        std::move(view_owner_request));

  story_info_->is_running = true;

  mojo::InterfaceHandle<ModuleWatcher> module_watcher;
  module_watcher_binding_.Bind(GetProxy(&module_watcher));
  module_->Watch(std::move(module_watcher));
}

void StoryImpl::Done() {
  FTL_LOG(INFO) << "StoryImpl::Done()";
  Stop();

  // Deleting |this| causes |Story| interface to be closed which is an
  // indication for UserShell that this story has terminated.
  delete this;
}

void StoryImpl::GetInfo(const GetInfoCallback& callback) {
  callback.Run(story_info_->Clone());
}

void StoryImpl::Stop() {
  FTL_LOG(INFO) << "StoryImpl::Stop()";
  if (!story_info_->is_running) {
    return;
  }

  module_.reset();
  session_.reset();
  runner_.reset();
  module_watcher_binding_.Close();
  story_provider_impl_->CommitStory(this);
  story_info_->is_running = false;
}

void StoryImpl::Start(
    mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  FTL_LOG(INFO) << "StoryImpl::Start()";
  if (story_info_->is_running) {
    return;
  }

  story_provider_impl_->ResumeStory(this, std::move(view_owner_request));
}

}  // namespace modular
