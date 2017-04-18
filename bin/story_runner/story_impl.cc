// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/story_impl.h"

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "application/services/application_launcher.fidl.h"
#include "application/services/service_provider.fidl.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/maxwell/services/user/user_intelligence_provider.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/services/module/module_context.fidl.h"
#include "apps/modular/services/module/module_data.fidl.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/services/story/story_marker.fidl.h"
#include "apps/modular/src/story_runner/link_impl.h"
#include "apps/modular/src/story_runner/module_context_impl.h"
#include "apps/modular/src/story_runner/module_controller_impl.h"
#include "apps/modular/src/story_runner/story_provider_impl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

constexpr char kStoryScopeLabelPrefix[] = "story-";

class StoryImpl::StoryMarkerImpl : StoryMarker {
 public:
  StoryMarkerImpl() = default;
  ~StoryMarkerImpl() override = default;

  void Connect(fidl::InterfaceRequest<StoryMarker> request) {
    bindings_.AddBinding(this, std::move(request));
  }

 private:
  fidl::BindingSet<StoryMarker> bindings_;
  FTL_DISALLOW_COPY_AND_ASSIGN(StoryMarkerImpl);
};

class StoryImpl::AddModuleCall : Operation<void> {
 public:
  AddModuleCall(OperationContainer* const container,
                StoryImpl* const story_impl,
                const fidl::String& module_name,
                const fidl::String& module_url,
                const fidl::String& link_name,
                const ResultCall& done)
      : Operation(container, done),
        story_impl_(story_impl),
        module_name_(module_name),
        module_url_(module_url),
        link_name_(link_name) {
    Ready();
  }

 private:
  void Run() {
    story_impl_->story_storage_impl_->WriteModuleData(
        module_name_, module_url_, link_name_, [this] {
          if (story_impl_->running_) {
            story_impl_->StartRootModule(module_name_, module_url_, link_name_);
          }
          Done();
        });
  };

  StoryImpl* const story_impl_;
  const fidl::String module_name_;
  const fidl::String module_url_;
  const fidl::String link_name_;

  FTL_DISALLOW_COPY_AND_ASSIGN(AddModuleCall);
};

class StoryImpl::AddForCreateCall : Operation<void> {
 public:
  AddForCreateCall(OperationContainer* const container,
                   StoryImpl* const story_impl,
                   const fidl::String& module_name,
                   const fidl::String& module_url,
                   const fidl::String& link_name,
                   const fidl::String& link_json,
                   const ResultCall& done)
      : Operation(container, done),
        story_impl_(story_impl),
        module_name_(module_name),
        module_url_(module_url),
        link_name_(link_name),
        link_json_(link_json) {
    Ready();
  }

 private:
  void Run() {
    if (link_json_.is_null()) {
      done_link_ = true;
    } else {
      story_impl_->CreateLink(nullptr, link_name_, link_.NewRequest());
      link_->UpdateObject(nullptr, link_json_);
      link_->Sync([this] {
          done_link_ = true;
          CheckDone();
        });
    }

    new AddModuleCall(&operation_collection_, story_impl_,
                      module_name_, module_url_, link_name_,
                      [this] {
                        done_module_ = true;
                        CheckDone();
                      });
  }

  void CheckDone() {
    if (done_link_ && done_module_) {
      Done();
    }
  }

  StoryImpl* const story_impl_;  // not owned
  const fidl::String module_name_;
  const fidl::String module_url_;
  const fidl::String link_name_;
  const fidl::String link_json_;

  LinkPtr link_;
  bool done_link_{};
  bool done_module_{};

  OperationCollection operation_collection_;

  FTL_DISALLOW_COPY_AND_ASSIGN(AddForCreateCall);
};

class StoryImpl::StartCall : Operation<void> {
 public:
  StartCall(OperationContainer* const container,
            StoryImpl* const story_impl,
            fidl::InterfaceRequest<mozart::ViewOwner> request)
      : Operation(container, []{}),
        story_impl_(story_impl),
        request_(std::move(request)) {
    Ready();
  }

 private:
  void Run() {
    // If the story is running, we do nothing and close the view owner request.
    if (story_impl_->running_) {
      FTL_LOG(INFO) << "StoryImpl::StartCall() while already running: ignored.";
      Done();
      return;
    }

    story_impl_->StartStoryShell(std::move(request_));

    // Start the root module and then show it in the story shell.
    //
    // Start *all* the root modules, not just the first one, with their
    // respective links.
    story_impl_->story_storage_impl_->ReadModuleData(
        [this](fidl::Array<ModuleDataPtr> data) {
          for (auto& module_data : data) {
            if (module_data->module_path.size() == 1) {
              story_impl_->StartRootModule(
                  module_data->module_path[0],
                  module_data->url,
                  module_data->link);
            }
          }

          story_impl_->running_ = true;
          story_impl_->state_ = StoryState::STARTING;

          story_impl_->NotifyStateChange();

          Done();
        });
  };


  StoryImpl* const story_impl_;  // not owned
  fidl::InterfaceRequest<mozart::ViewOwner> request_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StartCall);
};

class StoryImpl::StopCall : Operation<void> {
 public:
  StopCall(OperationContainer* const container,
           StoryImpl* const story_impl,
           std::function<void()> done)
      : Operation(container, done),
        story_impl_(story_impl) {
    Ready();
  }

 private:
  void Run() {
    // At this point, we don't need to monitor the root modules for state
    // changes anymore, because the next state change of the story is triggered
    // by the Stop() call below.
    story_impl_->module_watcher_bindings_.CloseAllBindings();

    // At this point, we don't need notifications from disconnected
    // Links anymore, as they will all be disposed soon anyway.
    for (auto& link : story_impl_->links_) {
      link->set_orphaned_handler(nullptr);
    }

    // Tear down all connections with a ModuleController first, then the
    // links between them.
    connections_count_ = story_impl_->connections_.size();

    if (connections_count_ == 0) {
      StopStoryShell();
    } else {
      for (auto& connection : story_impl_->connections_) {
        connection.module_controller_impl->TearDown([this] {
            ConnectionDown();
          });
      }
    }
  }

  void ConnectionDown() {
    --connections_count_;
    if (connections_count_ > 0) {
      // Not the last call.
      return;
    }

    StopStoryShell();
  }

  void StopStoryShell() {
    story_impl_->story_shell_->Terminate([this] {
        StoryShellDown();
      });
  }

  void StoryShellDown() {
    story_impl_->story_shell_controller_.reset();
    story_impl_->story_shell_.reset();

    StopLinks();
  }

  void StopLinks() {
    links_count_ = story_impl_->links_.size();

    // There always is at least one root link.
    FTL_CHECK(links_count_ > 0);

    // The links don't need to be written now, because they all were written
    // when they were last changed, but we need to wait for the last write
    // request to finish, which is done with the Sync() request below.
    //
    // TODO(mesch): We really only need to Sync() on story_storage_impl_.
    for (auto& link : story_impl_->links_) {
      link->Sync([this] { LinkDown(); });
    }
  }

  void LinkDown() {
    --links_count_;
    if (links_count_ > 0) {
      // Not the last call.
      return;
    }

    Cleanup();
  }

  void Cleanup() {
    // Clear the remaining links and connections in case there are some left. At
    // this point, no DisposeLink() calls can arrive anymore.
    story_impl_->links_.clear();
    story_impl_->connections_.clear();

    story_impl_->running_ = false;
    story_impl_->state_ = StoryState::STOPPED;

    story_impl_->NotifyStateChange();

    Done();
  };

  StoryImpl* const story_impl_;  // not owned
  int connections_count_{};
  int links_count_{};

  FTL_DISALLOW_COPY_AND_ASSIGN(StopCall);
};

class StoryImpl::DeleteCall : Operation<void> {
 public:
  DeleteCall(OperationContainer* const container,
             StoryImpl* const story_impl,
             std::function<void()> done)
      : Operation(container, []{}),
        story_impl_(story_impl),
        done_(std::move(done)) {
    Ready();
  }

 private:
  void Run() {
    // No call to Done(), in order to block all further operations on the queue
    // until the instance is deleted.
    new StopCall(&operation_queue_, story_impl_, done_);
  }

  StoryImpl* const story_impl_;  // not owned

  // Not the result call of the Operation, because it's invoked without
  // unblocking the operation queue, to prevent subsequent operations from
  // executing until the instance is deleted, which cancels those operations.
  std::function<void()> done_;

  OperationQueue operation_queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DeleteCall);
};

StoryImpl::StoryImpl(const fidl::String& story_id,
                     ledger::PagePtr story_page,
                     StoryProviderImpl* const story_provider_impl)
    : story_id_(story_id),
      story_provider_impl_(story_provider_impl),
      story_page_(std::move(story_page)),
      story_storage_impl_(new StoryStorageImpl(story_page_.get())),
      story_scope_(story_provider_impl_->user_scope(),
                   kStoryScopeLabelPrefix + story_id_.get()),
      story_context_binding_(this),
      story_marker_impl_(new StoryMarkerImpl) {
  bindings_.set_on_empty_set_handler(
      [this] { story_provider_impl_->PurgeController(story_id_); });

  story_scope_.AddService<StoryMarker>(
      [this](fidl::InterfaceRequest<StoryMarker> request) {
        story_marker_impl_->Connect(std::move(request));
      });
}

StoryImpl::~StoryImpl() = default;

void StoryImpl::Connect(fidl::InterfaceRequest<StoryController> request) {
  bindings_.AddBinding(this, std::move(request));
}

// |StoryController|
void StoryImpl::GetInfo(const GetInfoCallback& callback) {
  // Synced such that if GetInfo() is called after Start() or Stop(), the state
  // after the previously invoked operation is returned.
  //
  // If this call enters a race with a StoryProvider.DeleteStory() call, it may
  // silently not return or return null, or return the story info before it was
  // deleted, depending on where it gets sequenced in the operation queues of
  // StoryImpl and StoryProviderImpl. The queues do not block each other,
  // however, because the call on the second queue is made in the done callback
  // of the operation on the first queue.
  //
  // This race is normal fidl concurrency behavior.
  new SyncCall(&operation_queue_, [this, callback] {
      story_provider_impl_->GetStoryInfo(story_id_, callback);
    });
}

// |StoryController|
void StoryImpl::SetInfoExtra(const fidl::String& name,
                             const fidl::String& value,
                             const SetInfoExtraCallback& callback) {
  story_provider_impl_->SetStoryInfoExtra(story_id_, name, value, callback);
}

void StoryImpl::AddForCreate(const fidl::String& module_name,
                             const fidl::String& module_url,
                             const fidl::String& link_name,
                             const fidl::String& link_json,
                             const std::function<void()>& done) {
  new AddForCreateCall(&operation_queue_, this, module_name, module_url,
                       link_name, link_json, done);
}

// |StoryController|
void StoryImpl::AddModule(const fidl::String& module_name,
                          const fidl::String& module_url,
                          const fidl::String& link_name) {
  new AddModuleCall(&operation_queue_, this,
                    module_name, module_url, link_name, []{});
}

// |StoryController|
void StoryImpl::Start(fidl::InterfaceRequest<mozart::ViewOwner> request) {
  new StartCall(&operation_queue_, this, std::move(request));
}

void StoryImpl::StartStoryShell(
    fidl::InterfaceRequest<mozart::ViewOwner> request) {
  app::ServiceProviderPtr story_shell_services;
  auto story_shell_launch_info = app::ApplicationLaunchInfo::New();
  story_shell_launch_info->services = story_shell_services.NewRequest();
  story_shell_launch_info->url = story_provider_impl_->story_shell().url;
  story_shell_launch_info->arguments =
      story_provider_impl_->story_shell().args.Clone();

  story_scope_.GetLauncher()->CreateApplication(
      std::move(story_shell_launch_info), story_shell_controller_.NewRequest());

  mozart::ViewProviderPtr story_shell_view_provider;
  ConnectToService(story_shell_services.get(),
                   story_shell_view_provider.NewRequest());

  StoryShellFactoryPtr story_shell_factory;
  ConnectToService(story_shell_services.get(),
                   story_shell_factory.NewRequest());

  story_shell_view_provider->CreateView(std::move(request), nullptr);

  story_shell_factory->Create(story_context_binding_.NewBinding(),
                              story_shell_.NewRequest());
}

void StoryImpl::StartRootModule(const fidl::String& module_name,
                                const fidl::String& url,
                                const fidl::String& link_name) {
  LinkPtr link;
  CreateLink(nullptr, link_name, link.NewRequest());

  ModuleControllerPtr module_controller;
  StartModuleInShell(nullptr, module_name, url, std::move(link), nullptr,
                     nullptr, module_controller.NewRequest(), 0L, "");

  // TODO(mesch): Watch all root modules and compute story state from that.
  if (module_name == kRootModuleName) {
    module_controller->Watch(module_watcher_bindings_.AddBinding(this));
  }
}

// |StoryController|
void StoryImpl::Watch(fidl::InterfaceHandle<StoryWatcher> watcher) {
  auto ptr = StoryWatcherPtr::Create(std::move(watcher));
  ptr->OnStateChange(state_);
  watchers_.AddInterfacePtr(std::move(ptr));
}

// |ModuleWatcher|
void StoryImpl::OnStateChange(const ModuleState state) {
  switch (state) {
    case ModuleState::STARTING:
      state_ = StoryState::STARTING;
      break;
    case ModuleState::RUNNING:
    case ModuleState::UNLINKED:
      state_ = StoryState::RUNNING;
      break;
    case ModuleState::STOPPED:
      state_ = StoryState::STOPPED;
      break;
    case ModuleState::DONE:
      state_ = StoryState::DONE;
      break;
    case ModuleState::ERROR:
      state_ = StoryState::ERROR;
      break;
  }

  NotifyStateChange();
}

void StoryImpl::NotifyStateChange() {
  watchers_.ForAllPtrs(
      [this](StoryWatcher* const watcher) { watcher->OnStateChange(state_); });

  // NOTE(mesch): This gets scheduled on the StoryProviderImpl Operation
  // queue. If the current StoryImpl Operation is part of a DeleteStory
  // Operation of the StoryProviderImpl, then the SetStoryState Operation gets
  // scheduled after the delete of the story is completed, and it will not write
  // anything. The Operation on the other queue is not part of this Operation,
  // so not subject to locking if it travels in wrong direction of the hierarchy
  // (the principle we follow is that an Operation in one container may sync on
  // the operation queue of something inside the container, but not something
  // outside the container; this way we prevent lock cycles).
  //
  // TODO(mesch): It would still be nicer if we could complete the State writing
  // inside this Operation. We need out own copy of the Page* for that.
  story_provider_impl_->SetStoryState(story_id_, running_, state_);
}

void StoryImpl::GetLink(fidl::InterfaceRequest<Link> request) {
  CreateLink(nullptr, kRootLink, std::move(request));
}

void StoryImpl::GetNamedLink(const fidl::String& name,
                             fidl::InterfaceRequest<Link> request) {
  CreateLink(nullptr, name, std::move(request));
}

void StoryImpl::ReleaseModule(
    ModuleControllerImpl* const module_controller_impl) {
  auto f = std::find_if(connections_.begin(), connections_.end(),
                        [module_controller_impl](const Connection& c) {
                          return c.module_controller_impl.get() ==
                                 module_controller_impl;
                        });
  FTL_DCHECK(f != connections_.end());
  f->module_controller_impl.release();
  connections_.erase(f);
}

void StoryImpl::CreateLink(const fidl::Array<fidl::String>& module_path,
                           const fidl::String& name,
                           fidl::InterfaceRequest<Link> request) {
  auto i = std::find_if(
      links_.begin(), links_.end(),
      [name, &module_path](const std::unique_ptr<LinkImpl>& l) {
        return l->module_path().Equals(module_path) && l->name() == name;
      });
  if (i != links_.end()) {
    (*i)->Connect(std::move(request));
    return;
  }

  auto* const link_impl =
      new LinkImpl(story_storage_impl_.get(), module_path, name);
  link_impl->Connect(std::move(request));
  links_.emplace_back(link_impl);
  link_impl->set_orphaned_handler(
      [this, link_impl] { DisposeLink(link_impl); });
}

void StoryImpl::DisposeLink(LinkImpl* const link) {
  auto f = std::find_if(
      links_.begin(), links_.end(),
      [link](const std::unique_ptr<LinkImpl>& l) { return l.get() == link; });
  FTL_DCHECK(f != links_.end());
  links_.erase(f);
}

uint64_t StoryImpl::StartModule(
    const fidl::Array<fidl::String>& parent_path,
    const fidl::String& module_name,
    const fidl::String& module_url,
    fidl::InterfaceHandle<Link> link,
    fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
    fidl::InterfaceRequest<ModuleController> module_controller_request,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  // We currently require a 1:1 relationship between module
  // application instances and Module service instances, because
  // flutter only allows one ViewOwner per flutter application,
  // and we need one ViewOwner instance per Module instance.

  // TODO(mesch): If a module instance under this path already exists,
  // update it (or at least discard it) rather than to create a
  // duplicate one.
  auto child_path = parent_path.Clone();
  child_path.push_back(module_name);

  // TODO(vardhan): Add this module to the StoryData.
  auto launch_info = app::ApplicationLaunchInfo::New();

  app::ServiceProviderPtr app_services;
  launch_info->services = app_services.NewRequest();
  launch_info->url = module_url;

  FTL_LOG(INFO) << "StoryImpl::StartModule() " << module_url;

  app::ApplicationControllerPtr application_controller;
  story_scope_.GetLauncher()->CreateApplication(
      std::move(launch_info), application_controller.NewRequest());

  mozart::ViewProviderPtr view_provider;
  ConnectToService(app_services.get(), view_provider.NewRequest());
  view_provider->CreateView(std::move(view_owner_request), nullptr);

  ModulePtr module;
  ConnectToService(app_services.get(), module.NewRequest());

  fidl::InterfaceHandle<ModuleContext> self;
  fidl::InterfaceRequest<ModuleContext> self_request = self.NewRequest();

  module->Initialize(std::move(self), std::move(link),
                     std::move(outgoing_services),
                     std::move(incoming_services));

  Connection connection;

  connection.module_controller_impl.reset(new ModuleControllerImpl(
      this, std::move(application_controller), std::move(module),
      std::move(module_controller_request)));

  ModuleContextInfo module_context_info = {
      story_provider_impl_->component_context_info(), this,
      story_provider_impl_->user_intelligence_provider()};

  const auto id = next_module_instance_id_++;
  connection.module_context_impl.reset(new ModuleContextImpl(
      std::move(child_path), module_context_info, id, module_url,
      connection.module_controller_impl.get(), std::move(self_request)));

  connections_.emplace_back(std::move(connection));

  return id;
}

void StoryImpl::StartModuleInShell(
    const fidl::Array<fidl::String>& parent_path,
    const fidl::String& module_name,
    const fidl::String& module_url,
    fidl::InterfaceHandle<Link> link,
    fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
    fidl::InterfaceRequest<ModuleController> module_controller_request,
    const uint64_t parent_id,
    const fidl::String& view_type) {
  mozart::ViewOwnerPtr view_owner;
  const uint64_t id = StartModule(
      parent_path, module_name, module_url, std::move(link),
      std::move(outgoing_services), std::move(incoming_services),
      std::move(module_controller_request), view_owner.NewRequest());
  // If this is called during Stop(), story_shell_ might already have been
  // reset. TODO(mesch): Then the whole operation should fail.
  if (story_shell_) {
    story_shell_->ConnectView(view_owner.PassInterfaceHandle(), id, parent_id,
                              view_type);
  }
}

const fidl::String& StoryImpl::GetStoryId() const {
  return story_id_;
}

void StoryImpl::StopForDelete(const StopCallback& done) {
  new DeleteCall(&operation_queue_, this, done);
}

void StoryImpl::Stop(const StopCallback& done) {
  new StopCall(&operation_queue_, this, done);
}

}  // namespace modular
