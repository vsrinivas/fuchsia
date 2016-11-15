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
      binding_(this, std::move(story)) {
  FTL_LOG(INFO) << "StoryConnection() " << this
                << (module_controller_impl_ ? " primary" : "");
}

StoryConnection::~StoryConnection() {
  FTL_LOG(INFO) << "~StoryConnection() " << this
                << (module_controller_impl_ ? " primary" : "");
}

void StoryConnection::CreateLink(const fidl::String& name,
                                 fidl::InterfaceRequest<Link> link) {
  FTL_LOG(INFO) << "StoryConnection::CreateLink() " << name;
  story_impl_->CreateLink(name, std::move(link));
}

void StoryConnection::StartModule(
    const fidl::String& query,
    fidl::InterfaceHandle<Link> link,
    fidl::InterfaceRequest<ModuleController> module_controller,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner) {
  FTL_LOG(INFO) << "StoryConnection::StartModule() " << query;
  story_impl_->StartModule(query, std::move(link), std::move(module_controller),
                           std::move(view_owner));
}

void StoryConnection::Done() {
  FTL_LOG(INFO) << "StoryConnection::Done()";
  if (module_controller_impl_) {
    module_controller_impl_->Done();
  }
}

StoryImpl::StoryImpl(std::shared_ptr<ApplicationContext> application_context,
                     fidl::InterfaceHandle<Resolver> resolver,
                     fidl::InterfaceHandle<StoryStorage> story_storage,
                     fidl::InterfaceRequest<StoryContext> story_context_request)
    : binding_(this),
      application_context_(application_context),
      page_(new StoryPage(std::move(story_storage))) {
  //FTL_LOG(INFO) << "StoryImpl()";
  resolver_.Bind(std::move(resolver));

  page_->Init(ftl::MakeCopyable([
    this, story_context_request = std::move(story_context_request)
  ]() mutable {
    // Only bind after we are actually able to handle method invocations.
    binding_.Bind(std::move(story_context_request));
  }));
}

StoryImpl::~StoryImpl() {
  FTL_LOG(INFO) << "~StoryImpl()";
}

void StoryImpl::Dispose(ModuleControllerImpl* const module_controller_impl) {
  auto f = std::find_if(connections_.begin(), connections_.end(),
                        [module_controller_impl](const Connection& c) {
                          return c.module_controller_impl.get() ==
                                 module_controller_impl;
                        });
  FTL_DCHECK(f != connections_.end());
  connections_.erase(f);

  FTL_LOG(INFO) << "StoryImpl::Dispose() " << connections_.size();
}

void StoryImpl::CreateLink(const fidl::String& name,
                           fidl::InterfaceRequest<Link> link) {
  links_.emplace_back(LinkImpl::New(page_, name, std::move(link)));
}

void StoryImpl::StartModule(
    const fidl::String& query,
    fidl::InterfaceHandle<Link> link,
    fidl::InterfaceRequest<ModuleController> module_controller_request,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  FTL_LOG(INFO) << "StoryImpl::StartModule()";
  resolver_->Resolve(
      query, ftl::MakeCopyable([
        this, link = std::move(link),
        module_controller_request = std::move(module_controller_request),
        view_owner_request = std::move(view_owner_request)
      ](fidl::String module_url) mutable {
        //FTL_LOG(INFO) << "StoryImpl::StartModule() resolver callback";

        auto launch_info = ApplicationLaunchInfo::New();

        ServiceProviderPtr app_services;
        launch_info->services = GetProxy(&app_services);
        launch_info->url = module_url;

        application_context_->launcher()->CreateApplication(
            std::move(launch_info), nullptr);

        mozart::ViewProviderPtr view_provider;
        ConnectToService(app_services.get(), fidl::GetProxy(&view_provider));

        ServiceProviderPtr view_services;
        view_provider->CreateView(std::move(view_owner_request),
                                  fidl::GetProxy(&view_services));

        ModulePtr module;
        ConnectToService(view_services.get(), fidl::GetProxy(&module));

        fidl::InterfaceHandle<Story> self;
        fidl::InterfaceRequest<Story> self_request = GetProxy(&self);

        module->Initialize(std::move(self), std::move(link));

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

  FTL_LOG(INFO) << "StoryImpl::Stop() " << connections_.size() << " "
                << teardown_.size();

  if (teardown_.size() != 1) {
    // A teardown is in flight, just piggyback on it.
    return;
  }

  // TODO(mesch): While a teardown is in flight, new links and modules
  // can still be created. Those will be missed here, and only caught
  // by the destructor.

  // Take down all Link instances and write them to Storage.
  //
  // TODO(mesch): Technically this should be possible in the callback
  // shortly before the teardown_/done() calls. However, there it
  // causes a crash, and it's not clear why: It looks as if the error
  // handler on the binding is called after the destructor, even
  // though this should not be possible as the connection is closed in
  // the destructor.
  //
  // TODO(mesch): The Link destructor just sends the data off to the
  // ledger. There is no guarantee that, once this method here
  // returns, the data will already be written. A TearDown() with
  // return is needed for the Link instances as well.
  links_.clear();
  page_.reset();

  auto cont = [this]() {
    if (!connections_.empty()) {
      // Not the last call.
      return;
    }

    for (auto done : teardown_) {
      done();
    }

    // Also closes own connection.
    delete this;

    FTL_LOG(INFO) << "StoryImpl::Stop() DONE";
  };

  if (connections_.empty()) {
    cont();
  } else {
    // First, get rid of all connections without a ModuleController.
    auto n = std::remove_if(connections_.begin(), connections_.end(),
                            [](Connection& c) {
                              return c.module_controller_impl.get() == nullptr;
                            });
    connections_.erase(n, connections_.end());

    // Second, tear down all connections with a ModuleController.
    for (auto& connection : connections_) {
      connection.module_controller_impl->TearDown(cont);
    }
  }
}

StoryPage::StoryPage(fidl::InterfaceHandle<StoryStorage> story_storage)
    : data_(StoryData::New()) {
  //FTL_LOG(INFO) << "StoryPage()";
  data_->links.mark_non_null();
  story_storage_.Bind(std::move(story_storage));
};

StoryPage::~StoryPage() {
  //FTL_LOG(INFO) << "~StoryPage() " << this << " begin";

  // TODO(mesch): We should write on every link change, not just at
  // the end.
  story_storage_->WriteStoryData(std::move(data_));

  //FTL_LOG(INFO) << "~StoryPage() " << this << " end";
}

void StoryPage::Init(std::function<void()> done) {
  FTL_LOG(INFO) << "StoryPage::Init() " << this << to_string(id_) << " start";

  story_storage_->ReadStoryData(ftl::MakeCopyable([this,
                                                   done](StoryDataPtr data) {
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
  //FTL_LOG(INFO) << "StoryPage::MaybeReadlink() " << to_string(id_) << " "
  //              << name << " docs " << *docs_map;
}

void StoryPage::WriteLink(const fidl::String& name,
                          const FidlDocMap& docs_map) {
  FTL_LOG(INFO) << "StoryPage::WriteLink() " << to_string(id_) << " name "
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
