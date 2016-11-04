// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/story_runner/session.h"

#include "apps/document_store/interfaces/document.mojom.h"
#include "apps/modular/story_runner/link.h"
#include "apps/modular/services/story/link.mojom.h"
#include "apps/modular/services/story/resolver.mojom.h"
#include "apps/modular/services/story/session.mojom.h"
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
    SessionHost* const session,
    mojo::InterfacePtr<Module> module,
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
  FTL_LOG(INFO) << "SessionHost() primary " << this;
  impl_->Add(this);
}

SessionHost::SessionHost(
    SessionImpl* const impl,
    mojo::InterfaceRequest<Session> session,
    mojo::InterfacePtr<Module> module,
    mojo::InterfaceRequest<ModuleController> module_controller)
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
  FTL_LOG(INFO) << "~SessionHost() " << this;

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

void SessionHost::CreateLink(const mojo::String& name,
                             mojo::InterfaceRequest<Link> link) {
  FTL_LOG(INFO) << "SessionHost::CreateLink() " << name;
  impl_->CreateLink(name, std::move(link));
}

void SessionHost::StartModule(
    const mojo::String& query,
    mojo::InterfaceHandle<Link> link,
    mojo::InterfaceRequest<ModuleController> module_controller,
    mojo::InterfaceRequest<mozart::ViewOwner> view_owner) {
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

SessionImpl::SessionImpl(mojo::Shell* const shell,
                         mojo::InterfaceHandle<Resolver> resolver,
                         mojo::InterfaceHandle<ledger::Page> session_page,
                         mojo::InterfaceRequest<Session> req)
    : shell_(shell), page_(new SessionPage(std::move(session_page))) {

  FTL_LOG(INFO) << "SessionImpl()";
  resolver_.Bind(std::move(resolver));

  page_->Init(ftl::MakeCopyable([this, req = std::move(req)]() mutable {
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

void SessionImpl::CreateLink(const mojo::String& name,
                             mojo::InterfaceRequest<Link> link) {
  LinkImpl::New(page_, name, std::move(link));
}

void SessionImpl::StartModule(
    const mojo::String& query,
    mojo::InterfaceHandle<Link> link,
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

namespace {
std::string to_string(mojo::Array<uint8_t>& data) {
  std::string ret;
  ret.reserve(data.size());

  for (uint8_t val : data) {
    ret += std::to_string(val);
  }

  return ret;
}

mojo::Array<uint8_t> to_array(const std::string& val) {
  mojo::Array<uint8_t> ret;
  for (char c : val) {
    ret.push_back(c);
  }
  return ret;
}
}  // namespace

SessionPage::SessionPage(mojo::InterfaceHandle<ledger::Page> session_page)
    : data_(SessionData::New()) {
  data_->links.mark_non_null();

  FTL_LOG(INFO) << "SessionPage()";

  session_page_.Bind(std::move(session_page));

  session_page_->GetId([](mojo::Array<uint8_t> id) {
    FTL_LOG(INFO) << "story-runner init session with session page: "
                  << to_string(id);
  });
};

SessionPage::~SessionPage() {
  FTL_LOG(INFO) << "~SessionPage()";

  // TODO(mesch): We should write on every link change, not just at
  // the end.

  mojo::Array<uint8_t> bytes;
  bytes.resize(data_->GetSerializedSize());
  data_->Serialize(bytes.data(), bytes.size());

  // Return value callback is never invoked, because the pipe closes,
  // so we just pass an empty instance.
  session_page_->Put(to_array("session_data"), std::move(bytes),
                     ledger::Page::PutCallback());
}

void SessionPage::Init(std::function<void()> done) {
  session_page_->GetSnapshot(GetProxy(&session_page_snapshot_),
                             [](ledger::Status status) {});
  session_page_snapshot_->Get(
      to_array("session_data"),
      [this, done](ledger::Status status, ledger::ValuePtr value) {
        if (value) {
          data_->Deserialize(value->get_bytes().data(),
                             value->get_bytes().size());
        }
        done();
      });
}

void SessionPage::MaybeReadLink(const mojo::String& name,
                                MojoDocMap* const docs_map) const {
  auto i = data_->links.find(name);
  if (i != data_->links.end()) {
    for (const auto& doc : i.GetValue()->docs) {
      (*docs_map)[doc->docid] = doc->Clone();
    }
  }
}

void SessionPage::WriteLink(const mojo::String& name, const MojoDocMap& docs_map) {
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
