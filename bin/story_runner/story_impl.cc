// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/story_impl.h"

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/services/application/application_launcher.fidl.h"
#include "apps/modular/services/application/service_provider.fidl.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/services/story/resolver.fidl.h"
#include "apps/modular/services/story/story.fidl.h"
#include "apps/modular/src/story_runner/link_impl.h"
#include "apps/modular/src/story_runner/module_controller_impl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

StoryConnection::StoryConnection(
    StoryImpl* const story_impl,
    const std::string& module_url,
    ModuleControllerImpl* const module_controller_impl,
    fidl::InterfaceRequest<Story> story)
    : story_impl_(story_impl),
      module_url_(module_url),
      module_controller_impl_(module_controller_impl),
      binding_(this, std::move(story)) {}

void StoryConnection::CreateLink(const fidl::String& name,
                                 fidl::InterfaceRequest<Link> link) {
  story_impl_->CreateLink(name, std::move(link));
}

void StoryConnection::StartModule(
    const fidl::String& query,
    fidl::InterfaceHandle<Link> link,
    fidl::InterfaceHandle<ServiceProvider> outgoing_services,
    fidl::InterfaceRequest<ServiceProvider> incoming_services,
    fidl::InterfaceRequest<ModuleController> module_controller,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner) {
  story_impl_->StartModule(query, std::move(link), std::move(outgoing_services),
                           std::move(incoming_services),
                           std::move(module_controller), std::move(view_owner));
}

void StoryConnection::GetLedger(fidl::InterfaceRequest<ledger::Ledger> req,
                                const GetLedgerCallback& result) {
  if (!module_url_.empty()) {
    story_impl_->GetLedger(module_url_, std::move(req), result);
  } else {
    result(ledger::Status::UNKNOWN_ERROR);
  }
}

void StoryConnection::Ready() {
  if (module_controller_impl_) {
    module_controller_impl_->SetState(ModuleState::RUNNING);
  }
}

void StoryConnection::Done() {
  if (module_controller_impl_) {
    module_controller_impl_->SetState(ModuleState::DONE);
  }
}

StoryImpl::StoryImpl(
    ApplicationLauncher* const launcher,
    fidl::InterfaceHandle<Resolver> resolver,
    StoryStorageImpl* const story_storage,
    ledger::LedgerRepository* const ledger_repository)
    : launcher_(launcher),
      story_storage_(story_storage),
      ledger_repository_(ledger_repository) {
  resolver_.Bind(std::move(resolver));
}

StoryImpl::~StoryImpl() {}

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
  auto* const link_impl =
      new LinkImpl(story_storage_, name, std::move(request));
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
    const fidl::String& query,
    fidl::InterfaceHandle<Link> link,
    fidl::InterfaceHandle<ServiceProvider> outgoing_services,
    fidl::InterfaceRequest<ServiceProvider> incoming_services,
    fidl::InterfaceRequest<ModuleController> module_controller_request,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  resolver_->Resolve(
      query, ftl::MakeCopyable([
        this, link = std::move(link),
        outgoing_services = std::move(outgoing_services),
        incoming_services = std::move(incoming_services),
        module_controller_request = std::move(module_controller_request),
        view_owner_request = std::move(view_owner_request)
      ](fidl::String module_url) mutable {
        // We currently require a 1:1 relationship between module
        // application instances and Module service instances, because
        // flutter only allows one ViewOwner per flutter application,
        // and we need one ViewOwner instance per Module instance.

        auto launch_info = ApplicationLaunchInfo::New();

        ServiceProviderPtr app_services;
        launch_info->services = app_services.NewRequest();
        launch_info->url = module_url;

        FTL_LOG(INFO) << "StoryImpl::StartModule() " << module_url;

        ApplicationControllerPtr application_controller;
        launcher_->CreateApplication(
            std::move(launch_info), application_controller.NewRequest());

        mozart::ViewProviderPtr view_provider;
        ConnectToService(app_services.get(), view_provider.NewRequest());
        view_provider->CreateView(std::move(view_owner_request), nullptr);

        ModulePtr module;
        ConnectToService(app_services.get(), module.NewRequest());

        fidl::InterfaceHandle<Story> self;
        fidl::InterfaceRequest<Story> self_request = self.NewRequest();

        module->Initialize(std::move(self), std::move(link),
                           std::move(outgoing_services),
                           std::move(incoming_services));

        Connection connection;

        connection.module_controller_impl.reset(
            new ModuleControllerImpl(this, module_url,
                                     std::move(application_controller),
                                     std::move(module),
                                     std::move(module_controller_request)));

        connection.story_connection.reset(new StoryConnection(
            this, module_url, connection.module_controller_impl.get(),
            std::move(self_request)));

        connections_.emplace_back(std::move(connection));
      }));
}

void StoryImpl::GetLedger(const std::string& module_name,
                          fidl::InterfaceRequest<ledger::Ledger> request,
                          const std::function<void(ledger::Status)>& result) {
  FTL_DCHECK(!module_name.empty());
  ledger_repository_->GetLedger(to_array(module_name), std::move(request), result);
}

void StoryImpl::Stop(const std::function<void()>& done) {
  // TODO(mesch): Stop() is only ever called from StoryControllerImpl
  // anymore, and in a way that ensures only one Stop() invocation is
  // pending at a time. So this mechanism here is subsumed by the
  // pending queue in StoryControllerImpl and will be removed here
  // (actually the plan is to merge StoryImpl and
  // StoryControllerImpl).
  teardown_.push_back(done);

  if (teardown_.size() != 1) {
    // A teardown is in flight, just piggyback on it.
    return;
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

    StopLinks();
  };

  // Invocation or pass of cont must be last, as cont might delete
  // this via done callbacks.
  if (connections_.empty()) {
    cont();
  } else {
    for (auto& connection : connections_) {
      connection.module_controller_impl->TearDown(cont);
    }
  }
}

void StoryImpl::StopLinks() {
  auto links_count = std::make_shared<int>(links_.size());
  auto cont = [this, links_count] {
    --*links_count;
    if (*links_count > 0) {
      // Not the last call.
      return;
    }

    // Clear the remaining links. After they are destroyed, no
    // DisposeLink() calls can arrive anymore. They don't need to be
    // written, because they all were written when they were last
    // changed.
    links_.clear();

    // Done callbacks might delete |this| as well as objects provided
    // exclusively to |this| without ownership, and they are not
    // necessarily run through the runloop because they come in
    // through a non-fidl method. If the callbacks would be invoked
    // directly, |this| could be deleted not just for the remainder of
    // this function here, but also for the remainder of all functions
    // above us in the callstack, including functions that run as
    // methods of other objects owned by |this| or provided to |this|.
    //
    // (Specifically, this function is invoked as result callback from
    // SyncCall, which is an Operation instance in the OperationQueue
    // of StoryStorageImpl, which gets deleted together with StoryImpl
    // by StoryControllerImpl. SyncCall then goes on to call Done() to
    // remove itself from the OperationQueue, but at that time the
    // OperationQueue and all pending Operation instances in it would
    // already be deleted.)
    //
    // Therefore, to avoid such problems, all done callbacks are
    // invoked through the run loop.
    for (auto& done : teardown_) {
      mtl::MessageLoop::GetCurrent()->task_runner()->PostTask(done);
    }
  };

  // Invocation or pass of cont must be last, as cont might delete
  // this via done callbacks.
  if (links_.empty()) {
    cont();
  } else {
    for (auto& link : links_) {
      link->Sync(cont);
    }
  }
}

}  // namespace modular
