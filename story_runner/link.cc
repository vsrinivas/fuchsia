// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/story_runner/link.h"

#include "apps/modular/story_runner/link.mojom.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/struct_ptr.h"

namespace modular {

LinkHost::LinkHost(LinkImpl* const impl, mojo::InterfaceRequest<Link> req,
                   const bool primary)
    : impl_(impl), binding_(this, std::move(req)), primary_(primary) {
  FTL_LOG(INFO) << "LinkHost()" << (primary_ ? " primary" : "");
  impl_->Add(this);
}

LinkHost::~LinkHost() {
  FTL_LOG(INFO) << "~LinkHost()";
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

void LinkHost::SetValue(mojo::StructPtr<LinkValue> value) {
  impl_->SetValue(this, std::move(value));
}

void LinkHost::Value(const ValueCallback& callback) {
  callback.Run(impl_->Value().Clone());
}

void LinkHost::Watch(mojo::InterfaceHandle<LinkChanged> watcher) {
  AddWatcher(std::move(watcher), false);
}

void LinkHost::WatchAll(mojo::InterfaceHandle<LinkChanged> watcher) {
  AddWatcher(std::move(watcher), true);
}

void LinkHost::AddWatcher(mojo::InterfaceHandle<LinkChanged> watcher,
                          const bool self) {
  mojo::InterfacePtr<LinkChanged> watcher_ptr;
  watcher_ptr.Bind(watcher.Pass());

  // The current Value is sent to a newly registered watcher only if
  // it's not null.
  if (!impl_->Value().is_null()) {
    watcher_ptr->Value(impl_->Value().Clone());
  }

  watchers_.push_back(std::make_pair(std::move(watcher_ptr), self));
}

void LinkHost::Dup(mojo::InterfaceRequest<Link> dup) {
  new LinkHost(impl_, std::move(dup), false);
}

void LinkHost::Notify(LinkHost* const source,
                      const mojo::StructPtr<LinkValue>& value) {
  for (std::pair<mojo::InterfacePtr<LinkChanged>, bool>& watcher : watchers_) {
    if (watcher.second || this != source) {
      watcher.first->Value(value.Clone());
    }
  }
}

LinkImpl::LinkImpl(mojo::InterfaceRequest<Link> req) {
  FTL_LOG(INFO) << "LinkImpl()";
  new LinkHost(this, std::move(req), true);  // Calls Add().
}

LinkImpl::~LinkImpl() {
  while (!clients_.empty()) {
    delete clients_.back();  // Calls Remove(), which erases the
                             // deleted element.
  }
}

void LinkImpl::Add(LinkHost* const client) { clients_.push_back(client); }

void LinkImpl::Remove(LinkHost* const client) {
  auto f = std::find(clients_.begin(), clients_.end(), client);
  FTL_DCHECK(f != clients_.end());
  clients_.erase(f);
}

// SetValue knows which client a notification comes from, so it
// notifies only all other clients, or the ones that requested all
// notifications.
void LinkImpl::SetValue(LinkHost* const src, mojo::StructPtr<LinkValue> value) {
  value_ = std::move(value);
  for (auto dst : clients_) {
    dst->Notify(src, value_);
  }
}

const mojo::StructPtr<LinkValue>& LinkImpl::Value() const { return value_; }

}  // modular
