// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/user_runner/story_impl.h"

#include "apps/modular/user_runner/story_provider_impl.h"
#include "lib/ftl/logging.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/app/application_context.h"

namespace modular {

StoryImpl::StoryImpl(
    StoryInfoPtr story_info,
    StoryProviderImpl* const story_provider_impl,
    std::shared_ptr<ApplicationContext> application_context,
    fidl::InterfaceRequest<Story> story_request)
    : story_info_(std::move(story_info)),
      story_provider_impl_(story_provider_impl),
      storage_(story_provider_impl_->storage()),
      application_context_(application_context),
      binding_(this, std::move(story_request)),
      module_watcher_binding_(this),
      link_changed_binding_(this) {
  FTL_LOG(INFO) << "StoryImpl() " << story_info_->id;
}

StoryImpl::~StoryImpl() {
  FTL_LOG(INFO) << "~StoryImpl() " << story_info_->id;
}

// |Story|
void StoryImpl::GetInfo(const GetInfoCallback& callback) {
  callback(story_info_->Clone());
}

// |Story|
void StoryImpl::Start(
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  FTL_LOG(INFO) << "StoryImpl::Start() " << story_info_->id;

  if (story_info_->is_running) {
    return;
  }

  StartStoryRunner(std::move(view_owner_request));
  NotifyStoryWatchers(&StoryWatcher::OnStart);
}

// |Story|
void StoryImpl::Stop() {
  FTL_LOG(INFO) << "StoryImpl::Stop() " << story_info_->id;
  TearDownStoryRunner();
  NotifyStoryWatchers(&StoryWatcher::OnStop);
}

// |Story|
void StoryImpl::Watch(fidl::InterfaceHandle<StoryWatcher> story_watcher) {
  FTL_LOG(INFO) << "StoryImpl::Watch() " << story_info_->id;
  story_watchers_.emplace_back(
      StoryWatcherPtr::Create(std::move(story_watcher)));
}

// |ModuleWatcher|
void StoryImpl::Done() {
  FTL_LOG(INFO) << "StoryImpl::Done() " << story_info_->id;
  TearDownStoryRunner();
  NotifyStoryWatchers(&StoryWatcher::OnDone);
}

// |LinkChanged|
void StoryImpl::Notify(MojoDocMap docs) {
  FTL_LOG(INFO) << "StoryImpl::Notify() " << story_info_->id;
  NotifyStoryWatchers(&StoryWatcher::OnData);
}

void StoryImpl::NotifyStoryWatchers(void (StoryWatcher::*method)()) {
  for (auto& story_watcher : story_watchers_) {
    (story_watcher.get()->*method)();
  }
}

void StoryImpl::StartStoryRunner(
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  FTL_LOG(INFO) << "StoryImpl::StartStoryRunner() " << story_info_->id;

  auto story_runner_launch_info = ApplicationLaunchInfo::New();

  ServiceProviderPtr story_runner_app_services;
  story_runner_launch_info->services = GetProxy(&story_runner_app_services);
  story_runner_launch_info->url = "file:///system/apps/story_runner";

  application_context_->launcher()->CreateApplication(
      std::move(story_runner_launch_info), nullptr);

  ConnectToService(story_runner_app_services.get(), GetProxy(&runner_));


  auto resolver_launch_info = ApplicationLaunchInfo::New();

  ServiceProviderPtr resolver_app_services;
  resolver_launch_info->services = GetProxy(&resolver_app_services);
  resolver_launch_info->url = "file:///system/apps/resolver";

  application_context_->launcher()->CreateApplication(
      std::move(resolver_launch_info), nullptr);

  ResolverFactoryPtr resolver_factory;
  ConnectToService(resolver_app_services.get(), GetProxy(&resolver_factory));


  runner_->Initialize(std::move(resolver_factory));

  SessionStoragePtr session_storage;
  new SessionStorageImpl(storage_, story_info_->id, GetProxy(&session_storage));
  runner_->StartStory(std::move(session_storage), GetProxy(&session_));

  session_->CreateLink("root", GetProxy(&root_));

  fidl::InterfaceHandle<Link> link;
  root_->Dup(GetProxy(&link));
  session_->StartModule(story_info_->url, std::move(link), GetProxy(&module_),
                        std::move(view_owner_request));

  story_info_->is_running = true;
  story_provider_impl_->WriteStoryInfo(story_info_->Clone());

  fidl::InterfaceHandle<ModuleWatcher> module_watcher;
  module_watcher_binding_.Bind(GetProxy(&module_watcher));
  module_->Watch(std::move(module_watcher));

  fidl::InterfaceHandle<LinkChanged> link_changed;
  link_changed_binding_.Bind(GetProxy(&link_changed));
  root_->Watch(std::move(link_changed));
}

void StoryImpl::TearDownStoryRunner() {
  FTL_LOG(INFO) << "StoryImpl::TearDownStoryRunner() " << story_info_->id;

  // TODO(mesch): Here we need an actual call back when the Session is
  // down.

  module_.reset();
  session_.reset();
  runner_.reset();
  module_watcher_binding_.Close();

  story_info_->is_running = false;
  story_provider_impl_->WriteStoryInfo(story_info_->Clone());
}

}  // namespace modular
