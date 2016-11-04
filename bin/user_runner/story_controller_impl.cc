// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/story_controller_impl.h"

#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/src/user_runner/story_provider_impl.h"
#include "lib/ftl/logging.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/app/application_context.h"

namespace modular {

StoryControllerImpl::StoryControllerImpl(
    StoryInfoPtr story_info,
    StoryProviderImpl* const story_provider_impl,
    std::shared_ptr<ApplicationContext> application_context,
    fidl::InterfaceRequest<StoryController> story_controller_request)
    : story_info_(std::move(story_info)),
      story_provider_impl_(story_provider_impl),
      application_context_(application_context),
      binding_(this, std::move(story_controller_request)),
      module_watcher_binding_(this),
      link_changed_binding_(this) {
  FTL_LOG(INFO) << "StoryControllerImpl() " << story_info_->id
                << " " << to_string(story_info_->story_page_id);
}

StoryControllerImpl::~StoryControllerImpl() {
  FTL_LOG(INFO) << "~StoryControllerImpl() " << story_info_->id;
}

// |Story|
void StoryControllerImpl::GetInfo(const GetInfoCallback& callback) {
  callback(story_info_->Clone());
}

// |Story|
void StoryControllerImpl::Start(
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  FTL_LOG(INFO) << "StoryControllerImpl::Start() " << story_info_->id;

  if (story_info_->is_running) {
    return;
  }

  StartStoryRunner(std::move(view_owner_request));
  NotifyStoryWatchers(&StoryWatcher::OnStart);
}

// |Story|
void StoryControllerImpl::Stop() {
  FTL_LOG(INFO) << "StoryControllerImpl::Stop() " << story_info_->id;
  TearDownStoryRunner();
  NotifyStoryWatchers(&StoryWatcher::OnStop);
}

// |Story|
void StoryControllerImpl::Watch(
    fidl::InterfaceHandle<StoryWatcher> story_watcher) {
  FTL_LOG(INFO) << "StoryControllerImpl::Watch() " << story_info_->id;
  story_watchers_.emplace_back(
      StoryWatcherPtr::Create(std::move(story_watcher)));
}

// |ModuleWatcher|
void StoryControllerImpl::Done() {
  FTL_LOG(INFO) << "StoryControllerImpl::Done() " << story_info_->id;
  TearDownStoryRunner();
  NotifyStoryWatchers(&StoryWatcher::OnDone);
}

// |LinkChanged|
void StoryControllerImpl::Notify(FidlDocMap docs) {
  FTL_LOG(INFO) << "StoryControllerImpl::Notify() " << story_info_->id;
  NotifyStoryWatchers(&StoryWatcher::OnData);
}

void StoryControllerImpl::NotifyStoryWatchers(void (StoryWatcher::*method)()) {
  for (auto& story_watcher : story_watchers_) {
    (story_watcher.get()->*method)();
  }
}

void StoryControllerImpl::StartStoryRunner(
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  FTL_LOG(INFO) << "StoryControllerImpl::StartStoryRunner() "
                << story_info_->id;

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

  StoryStoragePtr story_storage;
  new StoryStorageImpl(
      story_provider_impl_->storage(),
      story_provider_impl_->GetStoryPage(story_info_->story_page_id),
      story_info_->id, GetProxy(&story_storage));
  runner_->StartStory(std::move(story_storage), GetProxy(&story_));

  story_->CreateLink("root", GetProxy(&root_));

  fidl::InterfaceHandle<Link> link;
  root_->Dup(GetProxy(&link));
  story_->StartModule(story_info_->url, std::move(link), GetProxy(&module_),
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

void StoryControllerImpl::TearDownStoryRunner() {
  FTL_LOG(INFO) << "StoryControllerImpl::TearDownStoryRunner() "
                << story_info_->id;

  // TODO(mesch): Here we need an actual call back when the Story is
  // down.

  // NOTE(mesch): For now we need to reset all handles we have on the
  // story, especially on links in the story, such that the
  // story data gets written to the ledger.
  root_.reset();
  link_changed_binding_.Close();
  module_.reset();
  story_.reset();
  runner_.reset();
  module_watcher_binding_.Close();

  story_info_->is_running = false;
  story_provider_impl_->WriteStoryInfo(story_info_->Clone());
}

}  // namespace modular
