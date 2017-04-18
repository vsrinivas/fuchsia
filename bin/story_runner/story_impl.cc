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

void StoryImpl::AddLinkDataAndSync(const fidl::String& json,
                                   const std::function<void()>& callback) {
  if (json.is_null()) {
    callback();
    return;
  }

  // TODO(mesch): Should not be special to the "root" link.
  EnsureRoot()->UpdateObject(nullptr, json);
  EnsureRoot()->Sync(callback);
}

// |StoryController|
void StoryImpl::GetInfo(const GetInfoCallback& callback) {
  // If a controller is deleted, we know there are no story data
  // anymore, and all connections to the controller are closed soon.
  // We just don't answer this request anymore and let its connection
  // get closed.
  if (deleted_) {
    FTL_LOG(INFO) << "StoryImpl::GetInfo() during delete: ignored.";
    return;
  }

  story_provider_impl_->GetStoryInfo(story_id_, callback);
}

// |StoryController|
void StoryImpl::SetInfoExtra(const fidl::String& name,
                             const fidl::String& value,
                             const SetInfoExtraCallback& callback) {
  if (deleted_) {
    FTL_LOG(INFO) << "StoryImpl::SetInfoExtra() during delete: ignored.";
    return;
  }

  story_provider_impl_->SetStoryInfoExtra(story_id_, name, value, callback);
}

void StoryImpl::AddModuleAndSync(const fidl::String& module_name,
                                 const fidl::String& module_url,
                                 const fidl::String& link_name,
                                 const std::function<void()>& done) {
  story_storage_impl_->WriteModuleData(module_name, module_url, link_name,
                                       done);
}

// |StoryController|
void StoryImpl::AddModule(const fidl::String& module_name,
                          const fidl::String& module_url,
                          const fidl::String& link_name) {
  if (deleted_) {
    FTL_LOG(INFO) << "StoryImpl::AddModule() during delete: ignored.";
    return;
  }

  AddModuleAndSync(module_name, module_url, link_name,
                   [this, module_name, module_url, link_name] {
                     if (running_) {
                       StartRootModule(module_name, module_url, link_name);
                     }
                   });
}

// |StoryController|
void StoryImpl::Start(fidl::InterfaceRequest<mozart::ViewOwner> request) {
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
    FTL_LOG(INFO) << "StoryImpl::Start() during delete: ignored.";
    return;
  }

  // If the story is running, we do nothing and close the view owner
  // request.
  if (running_) {
    FTL_LOG(INFO) << "StoryImpl::Start() while already running: ignored.";
    return;
  }

  // If another view owner request is pending, we close this one.
  // First start request wins.
  if (start_request_) {
    FTL_LOG(INFO) << "StoryImpl::Start() start request is pending: ignored.";
    return;
  }

  // We store the view owner request until we actually handle it. If
  // another start request arrives in the meantime, it is preempted by
  // this one.
  start_request_ = std::move(request);

  auto cont = [this] {
    if (start_request_ && !deleted_) {
      // Story shell can be used right after its start was requested.
      StartStoryShell(std::move(start_request_));

      // Start the root module and then show it in the story shell.
      //
      // Start *all* the root modules, not just the first one, with
      // their respective links.
      story_storage_impl_->ReadModuleData(
          [this](fidl::Array<ModuleDataPtr> data) {
            for (auto& module_data : data) {
              if (module_data->module_path.size() == 1) {
                StartRootModule(module_data->module_path[0], module_data->url,
                                module_data->link);
              }
            }

            running_ = true;
            state_ = StoryState::STARTING;

            NotifyStateChange();

            // In case we didn't use the start request, we close it here,
            start_request_ = nullptr;

            if (deleted_) {
              FTL_LOG(INFO)
                  << "StoryImpl::Start() callback during delete: ignored.";
            }
          });
    }
  };

  // If a stop request is in flight, we wait for it to finish before
  // we start.
  if (teardown_.size() > 0) {
    Stop(cont);
  } else {
    cont();
  }
}

void StoryImpl::StartStoryShell(
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
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

  story_shell_view_provider->CreateView(std::move(view_owner_request), nullptr);

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

  module_controller->Watch(module_watcher_bindings_.AddBinding(this));
  module_controllers_.emplace_back(std::move(module_controller));
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

  if (!deleted_) {
    // If the story controller is deleted, we do not write story data
    // anymore, because that would undelete it again.
    story_provider_impl_->SetStoryState(story_id_, running_, state_);
  }
}

LinkPtr& StoryImpl::EnsureRoot() {
  if (!root_.is_bound()) {
    CreateLink(nullptr, kRootLink, root_.NewRequest());
  }
  return root_;
}

void StoryImpl::GetLink(fidl::InterfaceRequest<Link> request) {
  EnsureRoot()->Dup(std::move(request));
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
  story_shell_->ConnectView(view_owner.PassInterfaceHandle(), id, parent_id,
                            view_type);
}

const fidl::String& StoryImpl::GetStoryId() const {
  return story_id_;
}

// A variant of Stop() that stops the controller because the story was
// deleted. It suppresses any further writes of story data, so that
// the story is not resurrected in the ledger. After this operation
// completes, Start() can not be called again. Once a StoryController
// instance received StopForDelete(), it cannot be reused anymore, and
// client connections will all be closed.
//
// TODO(mesch): A cleaner way is probably to retain tombstones in the
// ledger. We revisit that once we sort out cross device
// synchronization.
void StoryImpl::StopForDelete(const StopCallback& callback) {
  deleted_ = true;
  Stop(callback);
}

// |StoryController|
void StoryImpl::Stop(const StopCallback& done) {
  teardown_.emplace_back(done);

  if (teardown_.size() != 1) {
    // A teardown is in flight, just piggyback on it.
    return;
  }

  // At this point, we don't need to monitor the root module for state
  // changes anymore, because the next state change of the story is
  // triggered by the Stop() call below.
  module_watcher_bindings_.CloseAllBindings();

  // At this point, we don't need notifications from disconnected
  // Links anymore, as they will all be disposed soon anyway.
  for (auto& link : links_) {
    link->set_orphaned_handler(nullptr);
  }

  // NOTE(mesch): While a teardown is in flight, new links and modules
  // can still be created. Those would be missed here, but they would
  // just be torn down in the destructor.
  StopModules();
}

void StoryImpl::StopModules() {
  // Tear down all connections with a ModuleController first, then the
  // links between them.
  auto connections_count = std::make_shared<int>(connections_.size());
  auto cont = [this, connections_count] {
    --*connections_count;
    if (*connections_count > 0) {
      // Not the last call.
      return;
    }

    StopStoryShell();
  };

  if (connections_.empty()) {
    cont();
  } else {
    for (auto& connection : connections_) {
      connection.module_controller_impl->TearDown(cont);
    }
  }
}

void StoryImpl::StopStoryShell() {
  story_shell_->Terminate([this] {
    story_shell_controller_.reset();
    story_shell_.reset();
    StopLinks();
  });
}

void StoryImpl::StopLinks() {
  auto links_count = std::make_shared<int>(links_.size());
  auto cont = [this, links_count] {
    --*links_count;
    if (*links_count > 0) {
      // Not the last call.
      return;
    }

    // Clear the remaining links. At this point, no DisposeLink()
    // calls can arrive anymore.
    links_.clear();

    StopFinish();
  };

  // There always is a root link.
  FTL_CHECK(!links_.empty());

  // The links don't need to be written now, because they all were
  // written when they were last changed, but we need to wait for the
  // last write request to finish, which is done with the Sync()
  // request below.
  for (auto& link : links_) {
    link->Sync(cont);
  }
}

void StoryImpl::StopFinish() {
  running_ = false;
  state_ = StoryState::STOPPED;

  module_controllers_.clear();
  root_.reset();

  NotifyStateChange();

  // Done callbacks might delete |this| as well as objects provided
  // exclusively to |this| without ownership, and they are not
  // necessarily run through the runloop because they come in
  // through a non-fidl method. If the callbacks would be invoked
  // directly, |this| could be deleted not just for the remainder of
  // this function here, but also for the remainder of all functions
  // above us in the callstack, including functions that run as
  // methods of other objects owned by |this| or provided to |this|.
  // Therefore, to avoid such problems, all done callbacks are
  // invoked through the run loop.
  for (auto& done : teardown_) {
    mtl::MessageLoop::GetCurrent()->task_runner()->PostTask(done);
  }
}

}  // namespace modular
