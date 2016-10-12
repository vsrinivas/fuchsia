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

using mojo::InterfaceRequest;
using mojo::StructPtr;

struct SharedLinkImplData {
  StructPtr<LinkValue> value;
  std::vector<LinkImpl*> impls;
};

LinkImpl::LinkImpl(InterfaceRequest<Link> req, SharedLinkImplData* shared)
    : primary_(shared == nullptr), binding_(this, std::move(req)) {
  FTL_LOG(INFO) << "LinkImpl()" << (primary_ ? " primary" : "");
  shared_ = shared ? shared : new SharedLinkImplData();
  AddImpl(this);
}

LinkImpl::~LinkImpl() {
  FTL_LOG(INFO) << "~LinkImpl()" << (primary_ ? " primary" : "");
  watchers_.clear();
  RemoveImpl(this);

  // If a "primary" (currently that's the first) connection goes down,
  // the whole implementation is deleted, taking down all remaining
  // connections. This corresponds to a strong binding on the first
  // connection, and regular bindings on all later ones. This is just
  // how it is and may be revised in the future.
  if (primary_) {
    while (!shared_->impls.empty()) {
      delete shared_->impls.back();  // Calls RemoveImpl(), which erases the
                                      // deleted element.
    }

    delete shared_;
  }
}

void LinkImpl::New(InterfaceRequest<Link> req) {
  new LinkImpl(std::move(req), nullptr);
}

void LinkImpl::Value(const LinkImpl::ValueCallback& callback) {
  callback.Run(shared_->value.Clone());
}

void LinkImpl::Watch(mojo::InterfaceHandle<LinkChanged> watcher) {
  AddWatcher(std::move(watcher), false);
}

void LinkImpl::WatchAll(mojo::InterfaceHandle<LinkChanged> watcher) {
  AddWatcher(std::move(watcher), true);
}

void LinkImpl::AddWatcher(mojo::InterfaceHandle<LinkChanged> watcher,
                          const bool self) {
  mojo::InterfacePtr<LinkChanged> watcher_ptr;
  watcher_ptr.Bind(watcher.Pass());
  watchers_.emplace_back(std::make_pair(std::move(watcher_ptr), self));

  // The current Value is sent to a newly registered watcher only if
  // it's not null.
  if (!shared_->value.is_null()) {
    watchers_.back().first->Value(shared_->value.Clone());
  }
}

void LinkImpl::Notify(LinkImpl* const source,
                      const StructPtr<LinkValue>& value) {
  for (std::pair<mojo::InterfacePtr<LinkChanged>, bool>& watcher : watchers_) {
    if (watcher.second || this != source) {
      //TODO(jimbe) Watchers should actually be removed when they're closed.
      if (watcher.first.is_bound()) watcher.first->Value(value.Clone());
    }
  }
}

void LinkImpl::Dup(InterfaceRequest<Link> dup) {
  new LinkImpl(std::move(dup), shared_);
}

void LinkImpl::AddImpl(LinkImpl* client) { shared_->impls.push_back(client); }

void LinkImpl::RemoveImpl(LinkImpl* impl) {
  auto f = std::find(shared_->impls.rbegin(), shared_->impls.rend(), impl);
  FTL_DCHECK(f != shared_->impls.rend());
  shared_->impls.erase(std::next(f).base());
}

// SetValue knows which client a notification comes from, so it
// notifies only all other clients, or the ones that requested all
// notifications.
void LinkImpl::SetValue(StructPtr<LinkValue> value) {
  shared_->value = std::move(value);
  for (auto dst : shared_->impls) {
    dst->Notify(this, shared_->value);
  }
}

}  // modular
