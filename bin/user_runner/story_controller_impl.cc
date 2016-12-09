// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/story_controller_impl.h"

#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/src/user_runner/story_provider_impl.h"
#include "lib/ftl/logging.h"

namespace modular {

StoryControllerImpl::StoryControllerImpl(
    StoryDataPtr story_data,
    StoryProviderImpl* const story_provider_impl,
    UserLedgerRepositoryFactory* ledger_repository_factory)
    : story_data_(std::move(story_data)),
      story_provider_impl_(story_provider_impl),
      module_watcher_binding_(this),
      link_changed_binding_(this),
      ledger_repository_factory_(ledger_repository_factory) {
  bindings_.set_on_empty_set_handler([this]() {
    story_provider_impl_->PurgeControllers();
  });
}

void StoryControllerImpl::Connect(
    fidl::InterfaceRequest<StoryController> story_controller_request) {
  bindings_.AddBinding(this, std::move(story_controller_request));
}

// |StoryController|
void StoryControllerImpl::GetInfo(const GetInfoCallback& callback) {
  // If a controller is deleted, we know there are no story data
  // anymore, and all connections to the controller are closed soon.
  // We just don't answer this request anymore and let its connection
  // get closed.
  if (deleted_) {
    FTL_LOG(INFO) << "StoryControllerImpl::GetInfo() during delete: ignored.";
    return;
  }

  story_provider_impl_->GetStoryData(
      story_data_->story_info->id, [this, callback](StoryDataPtr story_data) {
        story_data_ = std::move(story_data);
        callback(story_data_->story_info->Clone());
      });
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
  // If a controller is stopped for delete, then it cannot be used
  // further. However, as of now nothing prevents a client to call
  // StartStory() on a story that is being deleted, so this condition
  // arises legitimately. We just do nothing, and the connection to
  // the client will be deleted shortly after. TODO(mesch): Change two
  // things: (1) API such that it can be notified about such
  // conditions, (2) implementation such that such conditons are
  // checked more systematically, e.g. implement a formal state
  // machine that checks how to handle each method in every state.
  if (deleted_) {
    FTL_LOG(INFO) << "StoryControllerImpl::StartStory() during delete: ignored.";
    return;
  }

  if (story_data_->story_info->is_running) {
    return;
  }

  StartStory(std::move(view_owner_request));
  NotifyStoryWatchers(&StoryWatcher::OnStart);
}

// |StoryController|
void StoryControllerImpl::Stop(const StopCallback& done) {
  if (!story_data_->story_info->is_running) {
    done();
    return;
  }

  story_runner_->Stop([this, done]() {
    story_data_->story_info->is_running = false;
    story_data_->story_info->state = StoryState::STOPPED;
    WriteStoryData([this, done]() {
      Reset();
      NotifyStoryWatchers(&StoryWatcher::OnStop);
      done();
    });
  });
}


// A variant of Stop() that stops the controller because the story was
// deleted. It also stops the story runner, but then does not write
// the story data to the ledger, because that would undelete the story
// again. It also suppresses writes of story data on status changes
// from the root module.
//
// TODO(mesch): A cleaner way is probably to retain tombstones in the
// ledger. We revisit that once we sort out cross device synchronization.
void StoryControllerImpl::StopForDelete(const StopCallback& done) {
  deleted_ = true;

  if (!story_data_->story_info->is_running) {
    done();
    return;
  }

  story_runner_->Stop([this, done]() {
    story_data_->story_info->is_running = false;
    story_data_->story_info->state = StoryState::STOPPED;
    Reset();
    NotifyStoryWatchers(&StoryWatcher::OnStop);
    done();
  });
}

void StoryControllerImpl::Reset() {
  root_.reset();
  link_changed_binding_.Close();
  module_.reset();
  story_.reset();
  story_runner_.reset();
  module_watcher_binding_.Close();
}

// |StoryController|
void StoryControllerImpl::Watch(
    fidl::InterfaceHandle<StoryWatcher> story_watcher) {
  story_watchers_.AddInterfacePtr(
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
  // If the story controller is deleted, we do not write story data
  // anymore, because that would undelete it again.
  if (!deleted_) {
    story_provider_impl_->WriteStoryData(story_data_->Clone(), done);
  } else {
    done();
  }
}

void StoryControllerImpl::NotifyStoryWatchers(void (StoryWatcher::*method)()) {
  story_watchers_.ForAllPtrs([method] (StoryWatcher* const watcher) {
    (watcher->*method)();
  });
}

void StoryControllerImpl::StartStory(
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  StoryRunnerFactoryPtr story_runner_factory;
  story_provider_impl_->ConnectToStoryRunnerFactory(story_runner_factory.NewRequest());

  ResolverPtr resolver;
  story_provider_impl_->ConnectToResolver(resolver.NewRequest());

  StoryStoragePtr story_storage;
  story_storage_impl_.reset(new StoryStorageImpl(
      story_provider_impl_->storage(),
      story_provider_impl_->GetStoryPage(story_data_->story_page_id),
      story_data_->story_info->id, story_storage.NewRequest()));

  story_runner_factory->Create(
      std::move(resolver), std::move(story_storage),
      ledger_repository_factory_->Clone(),
      story_runner_.NewRequest());

  story_runner_->GetStory(story_.NewRequest());
  story_->CreateLink("root", root_.NewRequest());

  if (!root_docs_.is_null()) {
    root_->AddDocuments(std::move(root_docs_));
  }

  // Connect pending requests to the new root_, so that it can be
  // configured before the new module uses it.
  for (auto& root_request : root_requests_) {
    root_->Dup(std::move(root_request));
  }
  root_requests_.clear();

  fidl::InterfaceHandle<Link> link;
  root_->Dup(link.NewRequest());
  story_->StartModule(story_data_->story_info->url, std::move(link), nullptr, nullptr,
                      module_.NewRequest(), std::move(view_owner_request));

  story_data_->story_info->is_running = true;
  story_data_->story_info->state = StoryState::RUNNING;
  WriteStoryData([](){});

  fidl::InterfaceHandle<ModuleWatcher> module_watcher;
  module_watcher_binding_.Bind(module_watcher.NewRequest());
  module_->Watch(std::move(module_watcher));

  fidl::InterfaceHandle<LinkWatcher> link_changed;
  link_changed_binding_.Bind(link_changed.NewRequest());
  root_->Watch(std::move(link_changed));
}

void StoryControllerImpl::GetLink(fidl::InterfaceRequest<Link> link_request) {
  if (root_.is_bound()) {
    root_->Dup(std::move(link_request));
  } else {
    root_requests_.emplace_back(std::move(link_request));
  }
}

}  // namespace modular
