// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/story_impl.h"

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
    ModuleControllerImpl* const module_controller_impl,
    fidl::InterfaceRequest<Story> story)
    : story_impl_(story_impl),
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

void StoryConnection::Done() {
  if (module_controller_impl_) {
    module_controller_impl_->Done();
  }
}

StoryImpl::StoryImpl(std::shared_ptr<ApplicationContext> application_context,
                     fidl::InterfaceHandle<Resolver> resolver,
                     fidl::InterfaceHandle<StoryStorage> story_storage,
                     fidl::InterfaceRequest<StoryContext> story_context_request)
    : binding_(this), application_context_(application_context) {
  resolver_.Bind(std::move(resolver));
  story_storage_.Bind(std::move(story_storage));
  binding_.Bind(std::move(story_context_request));
}

void StoryImpl::Dispose(ModuleControllerImpl* const module_controller_impl) {
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
  story_storage_->Dup(GetProxy(&story_storage_dup));
  links_.emplace_back(
      new LinkImpl(std::move(story_storage_dup), name, std::move(link)));
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
        auto launch_info = ApplicationLaunchInfo::New();

        ServiceProviderPtr app_services;
        launch_info->services = GetProxy(&app_services);
        launch_info->url = module_url;

        application_context_->launcher()->CreateApplication(
            std::move(launch_info), nullptr);

        mozart::ViewProviderPtr view_provider;
        ConnectToService(app_services.get(), fidl::GetProxy(&view_provider));
        view_provider->CreateView(std::move(view_owner_request), nullptr);

        ModulePtr module;
        ConnectToService(app_services.get(), fidl::GetProxy(&module));

        fidl::InterfaceHandle<Story> self;
        fidl::InterfaceRequest<Story> self_request = GetProxy(&self);

        module->Initialize(std::move(self), std::move(link),
            std::move(outgoing_services), std::move(incoming_services));

        Connection connection;

        connection.module_controller_impl.reset(
            new ModuleControllerImpl(this, module_url, std::move(module),
                                     std::move(module_controller_request)));

        connection.story_connection.reset(
            new StoryConnection(this, connection.module_controller_impl.get(),
                                std::move(self_request)));

        connections_.emplace_back(std::move(connection));
      }));
}

// |StoryContext|
void StoryImpl::GetStory(fidl::InterfaceRequest<Story> story_request) {
  Connection connection;

  connection.story_connection.reset(
      new StoryConnection(this, nullptr, std::move(story_request)));

  connections_.emplace_back(std::move(connection));
}

// |StoryContext|
void StoryImpl::Stop(const StopCallback& done) {
  teardown_.push_back(done);

  if (teardown_.size() != 1) {
    // A teardown is in flight, just piggyback on it.
    return;
  }

  // TODO(mesch): While a teardown is in flight, new links and modules
  // can still be created. Those will be missed here, and only caught
  // by the destructor.

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
  auto count = std::make_shared<unsigned int>(0);
  auto cont = [this, count]() {
    if (++*count < links_.size()) {
      return;
    }

    links_.clear();

    for (auto done : teardown_) {
      done();
    }

    // Also closes own connection.
    delete this;
  };

  if (links_.empty()) {
    cont();
  } else {
    for (auto& link : links_) {
      link->WriteLinkData(cont);
    }
  }
}

}  // namespace modular
