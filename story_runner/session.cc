// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/story_runner/session.h"

#include "apps/modular/story_runner/link.h"
#include "apps/modular/story_runner/link.mojom.h"
#include "apps/modular/story_runner/resolver.mojom.h"
#include "apps/modular/story_runner/session.mojom.h"
#include "apps/mozart/services/views/interfaces/view_provider.mojom.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/struct_ptr.h"
#include "mojo/public/interfaces/application/service_provider.mojom.h"

namespace modular {

ModuleControllerImpl::ModuleControllerImpl(
    SessionHost* const session, mojo::InterfacePtr<Module> module,
    mojo::InterfaceRequest<ModuleController> module_controller)
    : session_(session),
      binding_(this, std::move(module_controller)),
      module_(std::move(module)) {
  session_->Add(this);
  FTL_LOG(INFO) << "ModuleControllerImpl";
}

ModuleControllerImpl::~ModuleControllerImpl() {
  FTL_LOG(INFO) << "~ModuleControllerImpl " << this;
  session_->Remove(this);
}

void ModuleControllerImpl::Done() {
  FTL_LOG(INFO) << "ModuleControllerImpl::Done()";
  module_.reset();
  for (auto& watcher : watchers_) {
    watcher->Done();
  }
}

void ModuleControllerImpl::Watch(mojo::InterfaceHandle<ModuleWatcher> watcher) {
  watchers_.push_back(
      mojo::InterfacePtr<ModuleWatcher>::Create(std::move(watcher)));
}

SessionHost::SessionHost(SessionImpl* const impl,
                         mojo::InterfaceRequest<Session> session)
    : impl_(impl),
      binding_(this, std::move(session)),
      module_controller_(nullptr),
      primary_(true) {
  FTL_LOG(INFO) << "SessionHost() primary";
  impl_->Add(this);
}

SessionHost::SessionHost(
    SessionImpl* const impl, mojo::InterfaceRequest<Session> session,
    mojo::InterfacePtr<Module> module,
    mojo::InterfaceRequest<ModuleController> module_controller)
    : impl_(impl),
      binding_(this, std::move(session)),
      module_controller_(nullptr),
      primary_(false) {
  FTL_LOG(INFO) << "SessionHost()";
  impl_->Add(this);

  // Calls Add().
  new ModuleControllerImpl(this, std::move(module),
                           std::move(module_controller));
}

SessionHost::~SessionHost() {
  FTL_LOG(INFO) << "~SessionHost() " << this;

  if (module_controller_) {
    FTL_LOG(INFO) << "~SessionHost() delete module_controller "
                  << module_controller_;
    delete module_controller_;
  }

  impl_->Remove(this);

  // If a "primary" (currently that's the first) connection goes down,
  // the whole implementation is deleted, taking down all remaining
  // connections. This corresponds to a strong binding on the first
  // connection, and regular bindings on all later ones. This is just
  // how it is and may be revised in the future.
  //
  // Order is important: this delete call MUST happen after the
  // Remove() call above, otherwise double delete ensues.
  if (primary_) {
    delete impl_;
  }
}

void SessionHost::CreateLink(mojo::InterfaceRequest<Link> link) {
  FTL_LOG(INFO) << "SessionHost::CreateLink()";
  LinkImpl::New(std::move(link));
}

void SessionHost::StartModule(
    const mojo::String& query, mojo::InterfaceHandle<Link> link,
    mojo::InterfaceRequest<ModuleController> module_controller,
    mojo::InterfaceRequest<mozart::ViewOwner> view_owner) {
  FTL_LOG(INFO) << "SessionHost::StartModule()";
  impl_->StartModule(query, std::move(link), std::move(module_controller),
                     std::move(view_owner));
}

void SessionHost::Done() {
  FTL_LOG(INFO) << "SessionHost::Done()";
  if (module_controller_) {
    module_controller_->Done();
  }
}

void SessionHost::Add(ModuleControllerImpl* const module_controller) {
  module_controller_ = module_controller;
}

void SessionHost::Remove(ModuleControllerImpl* const module_controller) {
  module_controller_ = nullptr;
}

SessionImpl::SessionImpl(mojo::Shell* const shell,
                         mojo::InterfaceHandle<Resolver> resolver,
                         mojo::InterfaceHandle<ledger::Page> session_page,
                         mojo::InterfaceRequest<Session> req)
    : shell_(shell) {
  FTL_LOG(INFO) << "SessionImpl()";
  resolver_.Bind(std::move(resolver));
  session_page_.Bind(std::move(session_page));
  session_page_->GetId([](mojo::Array<uint8_t> id) {
    std::string string_id;
    for (uint8_t val : id) {
      string_id += std::to_string(val);
    }
    FTL_LOG(INFO) << "story-runner init session with session page: "
                  << string_id;
  });

  new SessionHost(this, std::move(req));  // Calls Add();
}

SessionImpl::~SessionImpl() {
  FTL_LOG(INFO) << "~SessionImpl()";
  while (!clients_.empty()) {
    delete clients_.back();  // Calls Remove(), which erases the
                             // deleted element.
  }
}

void SessionImpl::Add(SessionHost* const client) { clients_.push_back(client); }

void SessionImpl::Remove(SessionHost* const client) {
  auto f = std::find(clients_.begin(), clients_.end(), client);
  FTL_DCHECK(f != clients_.end());
  clients_.erase(f);
}

void SessionImpl::StartModule(
    const mojo::String& query, mojo::InterfaceHandle<Link> link,
    mojo::InterfaceRequest<ModuleController> module_controller,
    mojo::InterfaceRequest<mozart::ViewOwner> view_owner) {
  FTL_LOG(INFO) << "SessionImpl::StartModule()";
  resolver_->Resolve(
      query, ftl::MakeCopyable([
        this, link = std::move(link),
        module_controller = std::move(module_controller),
        view_owner = std::move(view_owner)
      ](mojo::String module_url) mutable {
        FTL_LOG(INFO) << "SessionImpl::StartModule() resolver callback";

        mojo::InterfacePtr<mozart::ViewProvider> view_provider;
        mojo::ConnectToService(shell_, module_url,
                               mojo::GetProxy(&view_provider));

        mojo::InterfacePtr<mojo::ServiceProvider> service_provider;
        view_provider->CreateView(std::move(view_owner),
                                  mojo::GetProxy(&service_provider));

        mojo::InterfacePtr<Module> module;
        service_provider->ConnectToService(Module::Name_,
                                           GetProxy(&module).PassMessagePipe());

        mojo::InterfaceHandle<Session> self;
        mojo::InterfaceRequest<Session> self_req = GetProxy(&self);

        module->Initialize(std::move(self), std::move(link));

        new SessionHost(this, std::move(self_req), std::move(module),
                        std::move(module_controller));
      }));
}

}  // namespace modular
