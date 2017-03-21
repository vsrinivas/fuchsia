// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/story_impl.h"

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "application/services/application_launcher.fidl.h"
#include "application/services/service_provider.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/services/module/module_context.fidl.h"
#include "apps/modular/services/story/link.fidl.h"
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

constexpr char kRootLink[] = "root";

StoryImpl::StoryImpl(StoryDataPtr story_data,
                     StoryProviderImpl* const story_provider_impl)
    : story_data_(std::move(story_data)),
      story_provider_impl_(story_provider_impl),
      story_context_binding_(this),
      module_watcher_binding_(this) {
  bindings_.set_on_empty_set_handler([this] {
    story_provider_impl_->PurgeController(story_data_->story_info->id);
  });

  story_storage_impl_.reset(new StoryStorageImpl(
      story_provider_impl_->storage(),
      story_provider_impl_->GetStoryPage(story_data_->story_page_id),
      story_data_->story_info->id));
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

  story_provider_impl_->GetStoryData(
      story_data_->story_info->id, [this, callback](StoryDataPtr story_data) {
        // TODO(mesch): It should not be necessary to read the data
        // from ledger again. Updates from the ledger should be
        // propagated to here and processed, and any change that
        // happens here should be written to the ledger such that it
        // can't be read again before it's written.
        story_data_ = std::move(story_data);
        callback(story_data_->story_info->Clone());
      });
}

// |StoryController|
void StoryImpl::SetInfoExtra(const fidl::String& name,
                             const fidl::String& value,
                             const SetInfoExtraCallback& callback) {
  story_data_->story_info->extra[name] = value;

  // Callback is serialized after WriteStoryData. This means that
  // after the callback returns, story info can be read from the
  // ledger and will have it.
  WriteStoryData(callback);
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
  if (story_data_->story_info->is_running) {
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
      // Start the root module and then show it in the story shell.
      mozart::ViewOwnerPtr root_module_view;
      StartRootModule(root_module_view.NewRequest());

      // Story shell can be used right after its start was requested.
      StartStoryShell(std::move(start_request_));
      story_shell_->ConnectView(std::move(root_module_view));

      NotifyStateChange();
    }

    // In case we didn't use the start request, we close it here,
    start_request_ = nullptr;

    if (deleted_) {
      FTL_LOG(INFO) << "StoryImpl::Start() callback during delete: ignored.";
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

  story_provider_impl_->launcher()->CreateApplication(
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

void StoryImpl::StartRootModule(
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  fidl::InterfaceHandle<Link> link;
  EnsureRoot()->Dup(link.NewRequest());

  StartModule(story_data_->story_info->url, std::move(link), nullptr, nullptr,
              module_.NewRequest(), std::move(view_owner_request));

  module_->Watch(module_watcher_binding_.NewBinding());

  story_data_->story_info->is_running = true;
  story_data_->story_info->state = StoryState::STARTING;

  WriteStoryData([] {});
}

// |StoryController|
void StoryImpl::Watch(fidl::InterfaceHandle<StoryWatcher> watcher) {
  auto ptr = StoryWatcherPtr::Create(std::move(watcher));
  const StoryState state = story_data_->story_info->state;
  ptr->OnStateChange(state);
  watchers_.AddInterfacePtr(std::move(ptr));
}

// |ModuleWatcher|
void StoryImpl::OnStateChange(const ModuleState state) {
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

  WriteStoryData([] {});
  NotifyStateChange();
}

void StoryImpl::WriteStoryData(std::function<void()> callback) {
  // If the story controller is deleted, we do not write story data
  // anymore, because that would undelete it again.
  if (!deleted_) {
    story_provider_impl_->WriteStoryData(story_data_->Clone(), callback);
  } else {
    callback();
  }
}

void StoryImpl::NotifyStateChange() {
  const StoryState state = story_data_->story_info->state;
  watchers_.ForAllPtrs(
      [state](StoryWatcher* const watcher) { watcher->OnStateChange(state); });
}

LinkPtr& StoryImpl::EnsureRoot() {
  if (!root_.is_bound()) {
    CreateLink(kRootLink, root_.NewRequest());
  }
  return root_;
}

void StoryImpl::GetLink(fidl::InterfaceRequest<Link> request) {
  CreateLink(kRootLink, std::move(request));
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

void StoryImpl::CreateLink(const fidl::String& name,
                           fidl::InterfaceRequest<Link> request) {
  auto i = std::find_if(
      links_.begin(), links_.end(),
      [name](const std::unique_ptr<LinkImpl>& l) { return l->name() == name; });
  if (i != links_.end()) {
    (*i)->Connect(std::move(request));
    return;
  }

  auto* const link_impl =
      new LinkImpl(story_storage_impl_.get(), name, std::move(request));
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

void StoryImpl::StartModule(
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

  auto launch_info = app::ApplicationLaunchInfo::New();

  app::ServiceProviderPtr app_services;
  launch_info->services = app_services.NewRequest();
  launch_info->url = module_url;

  FTL_LOG(INFO) << "StoryImpl::StartModule() " << module_url;

  app::ApplicationControllerPtr application_controller;
  story_provider_impl_->launcher()->CreateApplication(
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
      this, module_url, std::move(application_controller), std::move(module),
      std::move(module_controller_request)));

  connection.module_context_impl.reset(new ModuleContextImpl(
      this, module_url, connection.module_controller_impl.get(),
      story_provider_impl_->component_context_info(), std::move(self_request)));

  connections_.emplace_back(std::move(connection));
}

void StoryImpl::StartModuleInShell(
    const fidl::String& module_url,
    fidl::InterfaceHandle<Link> link,
    fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
    fidl::InterfaceRequest<ModuleController> module_controller_request) {
  mozart::ViewOwnerPtr view_owner;
  StartModule(module_url, std::move(link), std::move(outgoing_services),
              std::move(incoming_services),
              std::move(module_controller_request), view_owner.NewRequest());
  story_shell_->ConnectView(view_owner.PassInterfaceHandle());
}

const std::string& StoryImpl::GetStoryId() {
  return story_data_->story_info->id;
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
  if (module_watcher_binding_.is_bound()) {
    module_watcher_binding_.Close();
  }

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
  story_data_->story_info->is_running = false;
  story_data_->story_info->state = StoryState::STOPPED;

  module_.reset();
  root_.reset();

  WriteStoryData([this] {
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
  });
}

}  // namespace modular
