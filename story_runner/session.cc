// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/story_runner/session.h"

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/mojo/array_to_string.h"
#include "apps/modular/services/application/application_launcher.fidl.h"
#include "apps/modular/services/application/service_provider.fidl.h"
#include "apps/modular/services/document/document.fidl.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/services/story/resolver.fidl.h"
#include "apps/modular/services/story/session.fidl.h"
#include "apps/modular/story_runner/link.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"

namespace modular {

ModuleControllerImpl::ModuleControllerImpl(
    SessionHost* const session,
    fidl::InterfacePtr<Module> module,
    fidl::InterfaceRequest<ModuleController> module_controller)
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

void ModuleControllerImpl::Watch(fidl::InterfaceHandle<ModuleWatcher> watcher) {
  watchers_.push_back(
      fidl::InterfacePtr<ModuleWatcher>::Create(std::move(watcher)));
}

SessionHost::SessionHost(SessionImpl* const impl,
                         fidl::InterfaceRequest<Session> session)
    : impl_(impl),
      binding_(this, std::move(session)),
      module_controller_(nullptr),
      primary_(true) {
  FTL_LOG(INFO) << "SessionHost() primary " << this;
  impl_->Add(this);
}

SessionHost::SessionHost(
    SessionImpl* const impl,
    fidl::InterfaceRequest<Session> session,
    fidl::InterfacePtr<Module> module,
    fidl::InterfaceRequest<ModuleController> module_controller)
    : impl_(impl),
      binding_(this, std::move(session)),
      module_controller_(nullptr),
      primary_(false) {
  FTL_LOG(INFO) << "SessionHost() " << this;
  impl_->Add(this);

  // Calls Add().
  new ModuleControllerImpl(this, std::move(module),
                           std::move(module_controller));
}

SessionHost::~SessionHost() {
  FTL_LOG(INFO) << "~SessionHost() " << this << (primary_ ? " primary" : "");

  if (module_controller_) {
    FTL_LOG(INFO) << "~SessionHost() delete module_controller "
                  << module_controller_;
    delete module_controller_;
  }

  impl_->Remove(this);

  // If a "primary" (currently that's the first) connection goes down,
  // the whole implementation is shut down, taking down all remaining
  // connections. This corresponds to a strong binding on the first
  // connection, and regular bindings on all later ones. This is just
  // how it is and may be revised in the future.
  //
  // Order is important: this call MUST happen after the Remove() call
  // above, otherwise double delete ensues.
  if (primary_) {
    delete impl_;
  }
}

void SessionHost::CreateLink(const fidl::String& name,
                             fidl::InterfaceRequest<Link> link) {
  FTL_LOG(INFO) << "SessionHost::CreateLink() " << name;
  impl_->CreateLink(name, std::move(link));
}

void SessionHost::StartModule(
    const fidl::String& query,
    fidl::InterfaceHandle<Link> link,
    fidl::InterfaceRequest<ModuleController> module_controller,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner) {
  FTL_LOG(INFO) << "SessionHost::StartModule() " << query;
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

SessionImpl::SessionImpl(std::shared_ptr<ApplicationContext> application_context,
                         fidl::InterfaceHandle<Resolver> resolver,
                         fidl::InterfaceHandle<SessionStorage> session_storage,
                         fidl::InterfaceRequest<Session> req)
    : application_context_(application_context),
      page_(new SessionPage(std::move(session_storage))) {
  FTL_LOG(INFO) << "SessionImpl()";
  resolver_.Bind(std::move(resolver));

  page_->Init(ftl::MakeCopyable([ this, req = std::move(req) ]() mutable {
    new SessionHost(this, std::move(req));  // Calls Add();
  }));
}

SessionImpl::~SessionImpl() {
  FTL_LOG(INFO) << "~SessionImpl()";

  while (!clients_.empty()) {
    delete clients_.back();  // Calls Remove(), which erases the
                             // deleted element.
  }
}

void SessionImpl::Add(SessionHost* const client) {
  clients_.push_back(client);
}

void SessionImpl::Remove(SessionHost* const client) {
  auto f = std::find(clients_.begin(), clients_.end(), client);
  FTL_DCHECK(f != clients_.end());
  clients_.erase(f);
}

void SessionImpl::CreateLink(const fidl::String& name,
                             fidl::InterfaceRequest<Link> link) {
  LinkImpl::New(page_, name, std::move(link));
}

void SessionImpl::StartModule(
    const fidl::String& query,
    fidl::InterfaceHandle<Link> link,
    fidl::InterfaceRequest<ModuleController> module_controller,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner) {
  FTL_LOG(INFO) << "SessionImpl::StartModule()";
  resolver_->Resolve(
      query, ftl::MakeCopyable([
        this, link = std::move(link),
        module_controller = std::move(module_controller),
        view_owner = std::move(view_owner)
      ](fidl::String module_url) mutable {
        FTL_LOG(INFO) << "SessionImpl::StartModule() resolver callback";

        ApplicationLaunchInfoPtr launch_info = ApplicationLaunchInfo::New();

        ServiceProviderPtr app_services;
        launch_info->services = GetProxy(&app_services);

        application_context_->launcher()->CreateApplication(
            std::move(launch_info), nullptr);

        mozart::ViewProviderPtr view_provider;
        ConnectToService(app_services.get(), fidl::GetProxy(&view_provider));

        ServiceProviderPtr view_services;
        view_provider->CreateView(std::move(view_owner),
                                  fidl::GetProxy(&view_services));

        ModulePtr module;
        ConnectToService(view_services.get(), fidl::GetProxy(&module));

        fidl::InterfaceHandle<Session> self;
        fidl::InterfaceRequest<Session> self_req = GetProxy(&self);

        module->Initialize(std::move(self), std::move(link));

        new SessionHost(this, std::move(self_req), std::move(module),
                        std::move(module_controller));
      }));
}

SessionPage::SessionPage(fidl::InterfaceHandle<SessionStorage> session_storage)
    : data_(SessionData::New()) {
  FTL_LOG(INFO) << "SessionPage()";
  data_->links.mark_non_null();
  session_storage_.Bind(std::move(session_storage));
};

SessionPage::~SessionPage() {
  FTL_LOG(INFO) << "~SessionPage()";

  // TODO(mesch): We should write on every link change, not just at
  // the end.
  session_storage_->WriteSessionData(std::move(data_));
}

void SessionPage::Init(std::function<void()> done) {
  FTL_LOG(INFO) << "SessionPage::Init() " << to_string(id_);

  session_storage_->ReadSessionData(
      ftl::MakeCopyable([this, done](SessionDataPtr data) {
        if (!data.is_null()) {
          data_ = std::move(data);
        }
        done();
      }));
}

void SessionPage::MaybeReadLink(const fidl::String& name,
                                MojoDocMap* const docs_map) {
  auto i = data_->links.find(name);
  if (i != data_->links.end()) {
    for (const auto& doc : i.GetValue()->docs) {
      (*docs_map)[doc->docid] = doc->Clone();
    }
  }
  FTL_LOG(INFO) << "SessionPage::MaybeReadlink() " << to_string(id_) << " name "
                << name << " docs " << *docs_map;
}

void SessionPage::WriteLink(const fidl::String& name,
                            const MojoDocMap& docs_map) {
  FTL_LOG(INFO) << "SessionPage::WriteLink() " << to_string(id_) << " name "
                << name << " docs " << docs_map;

  auto i = data_->links.find(name);
  if (i == data_->links.end()) {
    data_->links[name] = LinkData::New();
  }

  auto& docs_list = data_->links[name]->docs;

  // Clear existing values in link data, and mark new link data
  // instance not null (there is no such method in Array, only in
  // Map).
  docs_list.resize(0);

  for (auto i = docs_map.cbegin(); i != docs_map.cend(); ++i) {
    docs_list.push_back(i.GetValue()->Clone());
  }
}

}  // namespace modular
