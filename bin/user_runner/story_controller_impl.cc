// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/story_controller_impl.h"

#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/src/user_runner/story_provider_impl.h"
#include "lib/ftl/logging.h"
#include "apps/modular/lib/app/connect.h"

namespace modular {

StoryControllerImpl::StoryControllerImpl(
    StoryDataPtr story_data,
    StoryProviderImpl* const story_provider_impl,
    ApplicationLauncherPtr launcher,
    fidl::InterfaceRequest<StoryController> story_controller_request)
    : story_data_(std::move(story_data)),
      story_provider_impl_(story_provider_impl),
      launcher_(std::move(launcher)),
      binding_(this, std::move(story_controller_request)),
      module_watcher_binding_(this),
      link_changed_binding_(this) {}

// |StoryController|
void StoryControllerImpl::GetInfo(const GetInfoCallback& callback) {
  callback(story_data_->story_info->Clone());
}

// |StoryController|
void StoryControllerImpl::SetInfoExtra(const fidl::String& name,
                                       const fidl::String& value,
                                       const SetInfoExtraCallback& callback) {
  story_data_->story_info->extra[name] = value;
  WriteStoryData(callback);
}

// |StoryController|
void StoryControllerImpl::Start(
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  if (story_data_->story_info->is_running) {
    return;
  }

  StartStory(std::move(view_owner_request));
  NotifyStoryWatchers(&StoryWatcher::OnStart);
}

// |StoryController|
void StoryControllerImpl::Stop(const StopCallback& done) {
  TearDownStory([this, done]() {
    NotifyStoryWatchers(&StoryWatcher::OnStop);
    done();
  });
}

// |StoryController|
void StoryControllerImpl::Watch(
    fidl::InterfaceHandle<StoryWatcher> story_watcher) {
  story_watchers_.emplace_back(
      StoryWatcherPtr::Create(std::move(story_watcher)));
}

// |ModuleWatcher|
void StoryControllerImpl::OnStop() {
  story_data_->story_info->state = StoryState::STOPPED;
  WriteStoryData([this]() {
    NotifyStoryWatchers(&StoryWatcher::OnStop);
  });
}

// |ModuleWatcher|
void StoryControllerImpl::OnDone() {
  story_data_->story_info->state = StoryState::DONE;
  WriteStoryData([this]() {
    NotifyStoryWatchers(&StoryWatcher::OnDone);
  });
}

// |ModuleWatcher|
void StoryControllerImpl::OnError() {
  story_data_->story_info->state = StoryState::ERROR;
  WriteStoryData([this]() {
    NotifyStoryWatchers(&StoryWatcher::OnError);
  });
}

// |LinkWatcher|
void StoryControllerImpl::Notify(FidlDocMap docs) {
  NotifyStoryWatchers(&StoryWatcher::OnData);
}

void StoryControllerImpl::WriteStoryData(std::function<void()> done) {
  story_provider_impl_->WriteStoryData(story_data_->Clone(), done);
}

void StoryControllerImpl::NotifyStoryWatchers(void (StoryWatcher::*method)()) {
  for (auto& story_watcher : story_watchers_) {
    (story_watcher.get()->*method)();
  }
}

void StoryControllerImpl::StartStory(
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  // NOTE(mesch): We start a new application for each of the services
  // we need here. Instead, we could have started the application once
  // at startup and just request a new service instance here.

  auto story_runner_launch_info = ApplicationLaunchInfo::New();
  ServiceProviderPtr story_runner_app_services;
  story_runner_launch_info->services = GetProxy(&story_runner_app_services);
  story_runner_launch_info->url = "file:///system/apps/story_runner";
  launcher_->CreateApplication(std::move(story_runner_launch_info), nullptr);
  StoryRunnerPtr story_runner;
  ConnectToService(story_runner_app_services.get(), GetProxy(&story_runner));

  auto resolver_launch_info = ApplicationLaunchInfo::New();
  ServiceProviderPtr resolver_app_services;
  resolver_launch_info->services = GetProxy(&resolver_app_services);
  resolver_launch_info->url = "file:///system/apps/resolver";
  launcher_->CreateApplication(std::move(resolver_launch_info), nullptr);
  ResolverPtr resolver;
  ConnectToService(resolver_app_services.get(), GetProxy(&resolver));

  StoryStoragePtr story_storage;
  story_storage_impl_.reset(new StoryStorageImpl(
      story_provider_impl_->storage(),
      story_provider_impl_->GetStoryPage(story_data_->story_page_id),
      story_data_->story_info->id, GetProxy(&story_storage)));

  story_runner->CreateStory(std::move(resolver), std::move(story_storage),
                            GetProxy(&story_context_));

  story_context_->GetStory(GetProxy(&story_));
  story_->CreateLink("root", GetProxy(&root_));

  fidl::InterfaceHandle<Link> link;
  root_->Dup(GetProxy(&link));
  story_->StartModule(story_data_->story_info->url, std::move(link), nullptr, nullptr,
                      GetProxy(&module_), std::move(view_owner_request));

  story_data_->story_info->is_running = true;
  story_data_->story_info->state = StoryState::RUNNING;
  WriteStoryData([](){});

  fidl::InterfaceHandle<ModuleWatcher> module_watcher;
  module_watcher_binding_.Bind(GetProxy(&module_watcher));
  module_->Watch(std::move(module_watcher));

  fidl::InterfaceHandle<LinkWatcher> link_changed;
  link_changed_binding_.Bind(GetProxy(&link_changed));
  root_->Watch(std::move(link_changed));
}

void StoryControllerImpl::TearDownStory(std::function<void()> done) {
  story_context_->Stop([this, done]() {
    story_data_->story_info->is_running = false;
    story_data_->story_info->state = StoryState::STOPPED;
    WriteStoryData([this, done]() {
        root_.reset();
        link_changed_binding_.Close();
        module_.reset();
        story_.reset();
        module_watcher_binding_.Close();

        done();
      });
  });
}

}  // namespace modular
