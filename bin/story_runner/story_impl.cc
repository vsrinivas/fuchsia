// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/story_impl.h"

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/services/application/application_launcher.fidl.h"
#include "apps/modular/services/application/service_provider.fidl.h"
#include "apps/modular/services/document_store/document.fidl.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/services/story/resolver.fidl.h"
#include "apps/modular/services/story/story.fidl.h"
#include "apps/modular/src/story_runner/link_impl.h"
#include "apps/modular/src/story_runner/module_controller_impl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

StoryConnection::StoryConnection(
    StoryImpl* const story_impl,
    const std::string& module_url,
    ModuleControllerImpl* const module_controller_impl,
    fidl::InterfaceRequest<Story> story)
    : story_impl_(story_impl), module_url_(module_url),
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
  story_impl_->StartModule(query, std::move(link),
                           std::move(outgoing_services),
                           std::move(incoming_services),
                           std::move(module_controller),
                           std::move(view_owner));
}

void StoryConnection::GetLedger(fidl::InterfaceRequest<ledger::Ledger> req,
                                const GetLedgerCallback& result) {
  if (!module_url_.empty()) {
    story_impl_->GetLedger(module_url_, std::move(req), result);
  } else {
    result(ledger::Status::UNKNOWN_ERROR);
  }
}

void StoryConnection::Done() {
  if (module_controller_impl_) {
    module_controller_impl_->Done();
  }
}

StoryImpl::StoryImpl(
    std::shared_ptr<ApplicationContext> application_context,
    fidl::InterfaceHandle<Resolver> resolver,
    fidl::InterfaceHandle<StoryStorage> story_storage,
    fidl::InterfaceHandle<ledger::LedgerRepository> user_ledger_repository,
    fidl::InterfaceRequest<StoryRunner> story_runner_request)
      : binding_(this), application_context_(application_context) {
  resolver_.Bind(std::move(resolver));
  story_storage_.Bind(std::move(story_storage));
  user_ledger_repository_.Bind(std::move(user_ledger_repository));
  binding_.Bind(std::move(story_runner_request));
}

void StoryImpl::DisposeModule(ModuleControllerImpl* const module_controller_impl) {
  auto f = std::find_if(connections_.begin(), connections_.end(),
                        [module_controller_impl](const Connection& c) {
                          return c.module_controller_impl.get() ==
                                 module_controller_impl;
                        });
  FTL_DCHECK(f != connections_.end());
  connections_.erase(f);
}

void StoryImpl::CreateLink(const fidl::String& name,
                           fidl::InterfaceRequest<Link> link) {
  StoryStoragePtr story_storage_dup;
  story_storage_->Dup(story_storage_dup.NewRequest());
  auto* link_impl = new LinkImpl(std::move(story_storage_dup), name, std::move(link));
  links_.emplace_back(link_impl);
  link_impl->set_orphaned_handler([this, link_impl]() { DisposeLink(link_impl); });
}

void StoryImpl::DisposeLink(LinkImpl* const link) {
  auto f = std::find_if(links_.begin(), links_.end(),
                        [link](const std::unique_ptr<LinkImpl>& l) {
                          return l.get() == link;
                        });
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
        // and we need one ViewOnwer instance per Module instance.

        auto launch_info = ApplicationLaunchInfo::New();

        ServiceProviderPtr app_services;
        launch_info->services = app_services.NewRequest();
        launch_info->url = module_url;

        ApplicationControllerPtr application_controller;
        application_context_->launcher()->CreateApplication(
            std::move(launch_info), application_controller.NewRequest());

        mozart::ViewProviderPtr view_provider;
        ConnectToService(app_services.get(), view_provider.NewRequest());
        view_provider->CreateView(std::move(view_owner_request), nullptr);

        ModulePtr module;
        ConnectToService(app_services.get(), module.NewRequest());

        fidl::InterfaceHandle<Story> self;
        fidl::InterfaceRequest<Story> self_request = self.NewRequest();

        module->Initialize(std::move(self), std::move(link),
            std::move(outgoing_services), std::move(incoming_services));

        Connection connection;

        connection.application_controller = std::move(application_controller);

        connection.module_controller_impl.reset(
            new ModuleControllerImpl(this, module_url, std::move(module),
                                     std::move(module_controller_request)));

        connection.story_connection.reset(
            new StoryConnection(this, module_url, connection.module_controller_impl.get(),
                                std::move(self_request)));

        connections_.emplace_back(std::move(connection));
      }));
}

void StoryImpl::GetLedger(
    const std::string& module_name,
    fidl::InterfaceRequest<ledger::Ledger> req,
    const std::function<void(ledger::Status)>& result) {
  FTL_DCHECK(!module_name.empty());
  user_ledger_repository_->GetLedger(to_array(module_name), std::move(req), result);
}

// |StoryRunner|
void StoryImpl::GetStory(fidl::InterfaceRequest<Story> story_request) {
  Connection connection;

  connection.story_connection.reset(
      new StoryConnection(this, "", nullptr, std::move(story_request)));

  connections_.emplace_back(std::move(connection));
}

// |StoryRunner|
void StoryImpl::Stop(const StopCallback& done) {
  teardown_.push_back(done);

  if (teardown_.size() != 1) {
    // A teardown is in flight, just piggyback on it.
    return;
  }

  // TODO(mesch): While a teardown is in flight, new links and modules
  // can still be created. Those would be missed here. A newly created
  // Module would actually block teardown, because no TearDown()
  // request would be issued to it, and thus the connections_
  // collection never becomes empty. A newly added Link would do no
  // harm and just be removed again.

  // At this point, we don't need notifications from disconnected
  // Links anymore, as they will all be disposed soon anyway.
  for (auto& link : links_) {
    link->set_orphaned_handler(nullptr);
  }

  StopModules();
}

void StoryImpl::StopModules() {
  auto cont = [this]() {
    if (!connections_.empty()) {
      // Not the last call.
      return;
    }

    StopLinks();
  };

  // First, get rid of all connections without a ModuleController.
  auto n = std::remove_if(
      connections_.begin(), connections_.end(),
      [](Connection& c) { return c.module_controller_impl.get() == nullptr; });
  connections_.erase(n, connections_.end());

  // Second, tear down all connections with a ModuleController.
  if (connections_.empty()) {
    cont();
  } else {
    for (auto& connection : connections_) {
      connection.module_controller_impl->TearDown(cont);
    }
  }
}

void StoryImpl::StopLinks() {
  // Clear the remaining links. After they are destroyed, no
  // DisposeLink() calls can arrive anymore. They don't need to be
  // written, because they all were written when they were last
  // changed.
  links_.clear();

  for (auto done : teardown_) {
    done();
  }

  // Also closes own connection, but the done callback to the Stop()
  // invocation is guaranteed to be sent.
  delete this;
}

}  // namespace modular
