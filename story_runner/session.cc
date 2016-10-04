// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/story_runner/session.h"

#include "apps/modular/story_runner/link.h"
#include "apps/modular/story_runner/link.mojom.h"
#include "apps/modular/story_runner/resolver.mojom.h"
#include "apps/modular/story_runner/session.mojom.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/struct_ptr.h"

namespace modular {

SessionHost::SessionHost(SessionImpl* const impl,
                         mojo::InterfaceRequest<Session> req,
                         const bool primary)
    : impl_(impl), binding_(this, std::move(req)), primary_(primary) {
  impl_->Add(this);
}

SessionHost::~SessionHost() {
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
  FTL_LOG(INFO) << "story-runner create link";
  new LinkImpl(std::move(link));
}

void SessionHost::StartModule(const mojo::String& query,
                              mojo::InterfaceHandle<Link> link,
                              const StartModuleCallback& callback) {
  impl_->StartModule(this, query, std::move(link), callback);
}

SessionImpl::SessionImpl(mojo::Shell* const shell,
                         mojo::InterfaceHandle<Resolver> resolver,
                         mojo::InterfaceHandle<ledger::Page> session_page,
                         mojo::InterfaceRequest<Session> req)
    : shell_(shell) {
  FTL_LOG(INFO) << "SessionImpl()";
  resolver_.Bind(resolver.Pass());
  session_page_.Bind(session_page.Pass());
  session_page_->GetId([](mojo::Array<uint8_t> id) {
    std::string string_id;
    for (uint8_t val : id) {
      string_id += std::to_string(val);
    }
    FTL_LOG(INFO) << "story-runner init session with session page: "
                  << string_id;
  });

  new SessionHost(this, std::move(req), true);  // Calls Add();
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
    SessionHost* const client, const mojo::String& query,
    mojo::InterfaceHandle<Link> link,
    const SessionHost::StartModuleCallback& callback) {
  const int link_id = new_link_id_();
  link_map_[link_id] = link.Pass();

  resolver_->Resolve(
      query, [client, this, link_id, callback](mojo::String module_url) {
        // TODO(mesch): Client is not yet used. We need to remember
        // the association of which module was requested from which
        // other module, and what link instance was exchanged
        // between them. We will do this by associating the link
        // instances with names which are local to the module that
        // uses them.

        mojo::InterfacePtr<Module> module;
        mojo::ConnectToService(shell_, module_url, GetProxy(&module));

        mojo::InterfaceHandle<Session> self;
        new SessionHost(this, GetProxy(&self), false);

        module->Initialize(std::move(self), link_map_[link_id].Pass());
        link_map_.erase(link_id);

        callback.Run(module.PassInterfaceHandle());
      });
}

}  // namespace modular
