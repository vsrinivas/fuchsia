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
    StoryProviderImpl* const story_provider_impl)
    : story_data_(std::move(story_data)),
      story_provider_impl_(story_provider_impl),
      module_watcher_binding_(this) {
  bindings_.set_on_empty_set_handler([this] {
    // This does not purge a controller with an open story runner as
    // indicated by IsActive().
    story_provider_impl_->PurgeController(story_data_->story_info->id);
  });
}

void StoryControllerImpl::Connect(
    fidl::InterfaceRequest<StoryController> story_controller_request) {
  bindings_.AddBinding(this, std::move(story_controller_request));
}

void StoryControllerImpl::AddLinkDataAndStop(FidlDocMap data,
                                             const StopCallback& done) {
  if (data.is_null()) {
    done();
    return;
  }

  if (!story_.is_bound()) {
    StartStoryRunner();
  }

  root_->AddDocuments(std::move(data));
  Stop(done);
}

bool StoryControllerImpl::IsActive() {
  return story_.is_bound() || stop_requests_.size() > 0 || start_request_;
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

  // Callback is serialized after WriteStoryData. This means that
  // after the callback returns, story info can be read from the
  // ledger and will have it. TODO(mesch): Not sure if that's needed.
  // Perhaps we can work this into a generalized Sync() operation?
  WriteStoryData(callback);
}

// |StoryController|
void StoryControllerImpl::Start(
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  // If a controller is stopped for delete, then it cannot be used
  // further. However, as of now nothing prevents a client to call
  // Start() on a story that is being deleted, so this condition
  // arises legitimately. We just do nothing, and the connection to
  // the client will be deleted shortly after. TODO(mesch): Change two
  // things: (1) API such that it can be notified about such
  // conditions, (2) implementation such that such conditons are
  // checked more systematically, e.g. implement a formal state
  // machine that checks how to handle each method in every state.
  if (deleted_) {
    FTL_LOG(INFO) << "StoryControllerImpl::Start() during delete: ignored.";
    return;
  }

  // If the story is running, we do nothing and close the view owner
  // request. Also, if another view owner request is pending, we close
  // this one. First start request wins.
  if (story_data_->story_info->is_running || start_request_) {
    return;
  }

  // We store the view owner request until we actually handle it. If
  // another start request arrives in the meantime, it is preempted by
  // this one.
  start_request_ = std::move(view_owner_request);

  auto cont = [this] {
    if (!story_.is_bound()) {
      StartStoryRunner();
    }

    if (start_request_ && !deleted_) {
      StartStory(std::move(start_request_));
      NotifyStateChange();
    }

    // In case we didn't use the start request, we close it here,
    start_request_ = nullptr;

    if (deleted_) {
      FTL_LOG(INFO) << "StoryControllerImpl::Start()"
                       " callback during delete: ignored.";
    }
  };

  // If a stop request is in flight, we wait for it to finish before
  // we start.
  if (stop_requests_.size() > 0) {
    Stop(cont);
  } else {
    cont();
  }
}

// |StoryController|
void StoryControllerImpl::Stop(const StopCallback& done) {
  stop_requests_.emplace_back(done);

  if (stop_requests_.size() != 1) {
    return;
  }

  if (!story_.is_bound()) {
    std::vector<std::function<void()>> stop_requests =
        std::move(stop_requests_);
    for (auto& done : stop_requests) {
      done();
    }
    return;
  }

  // At this point, we don't need to monitor the root module for state
  // changes anymore, because the next state change of the story is
  // triggered by the Stop() call below.
  if (module_watcher_binding_.is_bound()) {
    module_watcher_binding_.Close();
  }

  // Ensure the Stop() call is sent only after the root link was
  // written. Since the AddDocuments() and the Stop() requests are
  // sent over different channels, there is no ordering guarantee
  // between them. If the Stop() request happens to be executed before
  // the AddDocuments() request, the link is never written to the
  // ledger.
  root_->Sync([this] {
    story_runner_->Stop([this] {
      story_data_->story_info->is_running = false;
      story_data_->story_info->state = StoryState::STOPPED;
      WriteStoryData([]{});
      Reset();
      NotifyStateChange();

      std::vector<std::function<void()>> stop_requests =
          std::move(stop_requests_);
      for (auto& done : stop_requests) {
        done();
      }
    });
  });
}

// A variant of Stop() that stops the controller because the story was
// deleted. It suppresses any writes of story data, so that the story
// is not resurrected in the ledger.
//
// TODO(mesch): A cleaner way is probably to retain tombstones in the
// ledger. We revisit that once we sort out cross device
// synchronization.
void StoryControllerImpl::StopForDelete(const StopCallback& done) {
  deleted_ = true;
  Stop(done);
}

void StoryControllerImpl::Reset() {
  root_.reset();
  module_.reset();
  story_.reset();
  story_runner_.reset();
}

// |StoryController|
void StoryControllerImpl::Watch(fidl::InterfaceHandle<StoryWatcher> watcher) {
  auto ptr = StoryWatcherPtr::Create(std::move(watcher));
  const StoryState state = story_data_->story_info->state;
  ptr->OnStateChange(state);
  watchers_.AddInterfacePtr(std::move(ptr));
}

// |ModuleWatcher|
void StoryControllerImpl::OnStateChange(const ModuleState state) {
  switch (state) {
    case ModuleState::STARTING:
      story_data_->story_info->state = StoryState::STARTING;
      break;
    case ModuleState::RUNNING:
    case ModuleState::UNLINKED:
      story_data_->story_info->state = StoryState::RUNNING;
      break;
    case ModuleState::STOPPED:
      story_data_->story_info->state = StoryState::STOPPED;
      break;
    case ModuleState::DONE:
      story_data_->story_info->state = StoryState::DONE;
      break;
    case ModuleState::ERROR:
      story_data_->story_info->state = StoryState::ERROR;
      break;
  }

  WriteStoryData([]{});
  NotifyStateChange();
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

void StoryControllerImpl::NotifyStateChange() {
  const StoryState state = story_data_->story_info->state;
  watchers_.ForAllPtrs(
      [state](StoryWatcher* const watcher) { watcher->OnStateChange(state); });
}

void StoryControllerImpl::StartStoryRunner() {
  StoryRunnerFactoryPtr story_runner_factory;
  story_provider_impl_->ConnectToStoryRunnerFactory(
      story_runner_factory.NewRequest());

  ResolverPtr resolver;
  story_provider_impl_->ConnectToResolver(resolver.NewRequest());

  StoryStoragePtr story_storage;
  story_storage_impl_.reset(new StoryStorageImpl(
      story_provider_impl_->storage(),
      story_provider_impl_->GetStoryPage(story_data_->story_page_id),
      story_data_->story_info->id, story_storage.NewRequest()));

  story_runner_factory->Create(
      std::move(resolver), std::move(story_storage),
      story_provider_impl_->DuplicateLedgerRepository(),
      story_runner_.NewRequest());

  story_runner_->GetStory(story_.NewRequest());
  story_->CreateLink("root", root_.NewRequest());
}

void StoryControllerImpl::StartStory(
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  fidl::InterfaceHandle<Link> link;
  root_->Dup(link.NewRequest());
  story_->StartModule(story_data_->story_info->url, std::move(link), nullptr,
                      nullptr, module_.NewRequest(),
                      std::move(view_owner_request));

  story_data_->story_info->is_running = true;
  story_data_->story_info->state = StoryState::STARTING;
  WriteStoryData([]{});

  module_->Watch(module_watcher_binding_.NewBinding());
}

void StoryControllerImpl::GetLink(fidl::InterfaceRequest<Link> link_request) {
  if (!story_.is_bound()) {
    StartStoryRunner();
  }

  root_->Dup(std::move(link_request));
}

}  // namespace modular
