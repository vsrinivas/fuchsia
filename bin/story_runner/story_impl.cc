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
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"

namespace modular {

ModuleControllerImpl::ModuleControllerImpl(
    StoryHost* const story,
    fidl::InterfacePtr<Module> module,
    fidl::InterfaceRequest<ModuleController> module_controller)
    : story_(story),
      binding_(this, std::move(module_controller)),
      module_(std::move(module)) {
  FTL_LOG(INFO) << "ModuleControllerImpl " << this;
  story_->Add(this);
}

ModuleControllerImpl::~ModuleControllerImpl() {
  FTL_LOG(INFO) << "~ModuleControllerImpl " << this;
  story_->Remove(this);
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

StoryHost::StoryHost(StoryImpl* const impl, fidl::InterfaceRequest<Story> story)
    : impl_(impl),
      binding_(this, std::move(story)),
      module_controller_(nullptr),
      primary_(true) {
  FTL_LOG(INFO) << "StoryHost() primary " << this;
  impl_->Add(this);

  binding_.set_connection_error_handler([this]() {
    FTL_LOG(INFO) << "StoryHost() " << this << " connection closed";
  });
}

StoryHost::StoryHost(StoryImpl* const impl,
                     fidl::InterfaceRequest<Story> story,
                     fidl::InterfacePtr<Module> module,
                     fidl::InterfaceRequest<ModuleController> module_controller)
    : impl_(impl),
      binding_(this, std::move(story)),
      module_controller_(nullptr),
      primary_(false) {
  FTL_LOG(INFO) << "StoryHost() " << this;
  impl_->Add(this);

  // Calls Add().
  new ModuleControllerImpl(this, std::move(module),
                           std::move(module_controller));
}

StoryHost::~StoryHost() {
  FTL_LOG(INFO) << "~StoryHost() " << this << (primary_ ? " primary" : "");

  if (module_controller_) {
    FTL_LOG(INFO) << "~StoryHost() delete module_controller "
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

void StoryHost::CreateLink(const fidl::String& name,
                           fidl::InterfaceRequest<Link> link) {
  FTL_LOG(INFO) << "StoryHost::CreateLink() " << name;
  impl_->CreateLink(name, std::move(link));
}

void StoryHost::StartModule(
    const fidl::String& query,
    fidl::InterfaceHandle<Link> link,
    fidl::InterfaceRequest<ModuleController> module_controller,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner) {
  FTL_LOG(INFO) << "StoryHost::StartModule() " << query;
  impl_->StartModule(query, std::move(link), std::move(module_controller),
                     std::move(view_owner));
}

void StoryHost::Done() {
  FTL_LOG(INFO) << "StoryHost::Done()";
  if (module_controller_) {
    module_controller_->Done();
  }
}

void StoryHost::Add(ModuleControllerImpl* const module_controller) {
  module_controller_ = module_controller;
}

void StoryHost::Remove(ModuleControllerImpl* const module_controller) {
  module_controller_ = nullptr;
}

StoryImpl::StoryImpl(std::shared_ptr<ApplicationContext> application_context,
                     fidl::InterfaceHandle<Resolver> resolver,
                     fidl::InterfaceHandle<StoryStorage> story_storage,
                     fidl::InterfaceRequest<Story> req)
    : application_context_(application_context),
      page_(new StoryPage(std::move(story_storage))) {
  FTL_LOG(INFO) << "StoryImpl()";
  resolver_.Bind(std::move(resolver));

  page_->Init(ftl::MakeCopyable([ this, req = std::move(req) ]() mutable {
    new StoryHost(this, std::move(req));  // Calls Add();
  }));
}

StoryImpl::~StoryImpl() {
  FTL_LOG(INFO) << "~StoryImpl()";

  while (!clients_.empty()) {
    delete clients_.back();  // Calls Remove(), which erases the
                             // deleted element.
  }
}

void StoryImpl::Add(StoryHost* const client) {
  clients_.push_back(client);
}

void StoryImpl::Remove(StoryHost* const client) {
  auto f = std::find(clients_.begin(), clients_.end(), client);
  FTL_DCHECK(f != clients_.end());
  clients_.erase(f);
}

void StoryImpl::CreateLink(const fidl::String& name,
                           fidl::InterfaceRequest<Link> link) {
  LinkImpl::New(page_, name, std::move(link));
}

void StoryImpl::StartModule(
    const fidl::String& query,
    fidl::InterfaceHandle<Link> link,
    fidl::InterfaceRequest<ModuleController> module_controller,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner) {
  FTL_LOG(INFO) << "StoryImpl::StartModule()";
  resolver_->Resolve(
      query, ftl::MakeCopyable([
        this, link = std::move(link),
        module_controller = std::move(module_controller),
        view_owner = std::move(view_owner)
      ](fidl::String module_url) mutable {
        FTL_LOG(INFO) << "StoryImpl::StartModule() resolver callback";

        auto launch_info = ApplicationLaunchInfo::New();

        ServiceProviderPtr app_services;
        launch_info->services = GetProxy(&app_services);
        launch_info->url = module_url;

        application_context_->launcher()->CreateApplication(
            std::move(launch_info), nullptr);

        mozart::ViewProviderPtr view_provider;
        ConnectToService(app_services.get(), fidl::GetProxy(&view_provider));

        ServiceProviderPtr view_services;
        view_provider->CreateView(std::move(view_owner),
                                  fidl::GetProxy(&view_services));

        ModulePtr module;
        ConnectToService(view_services.get(), fidl::GetProxy(&module));

        fidl::InterfaceHandle<Story> self;
        fidl::InterfaceRequest<Story> self_req = GetProxy(&self);

        module->Initialize(std::move(self), std::move(link));

        new StoryHost(this, std::move(self_req), std::move(module),
                      std::move(module_controller));
      }));
}

StoryPage::StoryPage(fidl::InterfaceHandle<StoryStorage> story_storage)
    : data_(StoryData::New()) {
  FTL_LOG(INFO) << "StoryPage()";
  data_->links.mark_non_null();
  story_storage_.Bind(std::move(story_storage));
};

StoryPage::~StoryPage() {
  FTL_LOG(INFO) << "~StoryPage() " << this << " begin";

  // TODO(mesch): We should write on every link change, not just at
  // the end.
  story_storage_->WriteStoryData(std::move(data_));

  FTL_LOG(INFO) << "~StoryPage() " << this << " end";
}

void StoryPage::Init(std::function<void()> done) {
  FTL_LOG(INFO) << "StoryPage::Init() " << this
                << to_string(id_) << " start";

  story_storage_->ReadStoryData(
      ftl::MakeCopyable([this, done](StoryDataPtr data) {
        if (!data.is_null()) {
          data_ = std::move(data);
        }
        FTL_LOG(INFO) << "StoryPage::Init() " << this << to_string(id_) << " done";
        done();
      }));
}

void StoryPage::MaybeReadLink(const fidl::String& name,
                              FidlDocMap* const docs_map) {
  auto i = data_->links.find(name);
  if (i != data_->links.end()) {
    for (const auto& doc : i.GetValue()->docs) {
      (*docs_map)[doc->docid] = doc->Clone();
    }
  }
  FTL_LOG(INFO) << "StoryPage::MaybeReadlink() " << to_string(id_) << " "
                << name << " docs " << *docs_map;
}

void StoryPage::WriteLink(const fidl::String& name,
                          const FidlDocMap& docs_map) {
  FTL_LOG(INFO) << "StoryPage::WriteLink() " << to_string(id_)
                << " name " << name
                << " docs " << docs_map;

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
