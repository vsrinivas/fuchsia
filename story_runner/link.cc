// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/document_store/interfaces/document.mojom.h"
#include "apps/modular/document_editor/document_editor.h"
#include "apps/modular/story_runner/link.h"
#include "apps/modular/story_runner/link.mojom.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/struct_ptr.h"

namespace modular {

using document_store::Document;
using document_store::Property;
using document_store::Value;

using modular::DocumentEditor;

using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::StructPtr;

struct SharedLinkImplData {
  StructPtr<Document> doc;
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
      delete shared_->impls.back();   // Calls RemoveImpl(), which erases the
                                      // deleted element.
    }

    delete shared_;
  }
}

void LinkImpl::New(InterfaceRequest<Link> req) {
  new LinkImpl(std::move(req), nullptr);
}

void LinkImpl::Query(const LinkImpl::QueryCallback& callback) {
  callback.Run(shared_->doc.Clone());
}

void LinkImpl::Watch(InterfaceHandle<LinkChanged> watcher) {
  AddWatcher(std::move(watcher), false);
}

void LinkImpl::WatchAll(InterfaceHandle<LinkChanged> watcher) {
  AddWatcher(std::move(watcher), true);
}

void LinkImpl::AddWatcher(InterfaceHandle<LinkChanged> watcher,
                          const bool self) {
  InterfacePtr<LinkChanged> watcher_ptr;
  watcher_ptr.Bind(watcher.Pass());
  watchers_.emplace_back(std::make_pair(std::move(watcher_ptr), self));

  // The current Document is sent to a newly registered watcher only if
  // it's not null.
  // TODO(jimbe) Sending an initial notification to the watcher smells wrong.
  FTL_LOG(INFO) << "$$ Sending initial values notification";
  if (!shared_->doc.is_null() && !shared_->doc->properties.is_null()) {
    watchers_.back().first->Notify(shared_->doc.Clone());
  }
}

void LinkImpl::Notify(LinkImpl* source,
                      const StructPtr<Document>& doc) {
  for (std::pair<InterfacePtr<LinkChanged>, bool>& watcher : watchers_) {
    if (watcher.second || this != source) {
      //TODO(jimbe) Watchers should actually be removed when they're closed.
      if (watcher.first.is_bound()) watcher.first->Notify(doc.Clone());
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

// The |LinkImpl| object knows which client made the call to AddDocument(), so
// it notifies either all clients or all other clients, depending on whether
// WatchAll() or Watch() was called, respectively.
// TODO(jimbe) This mechanism breaks if the call to Watch() is made *after*
// the call to AddDocument(). Need to find a way to improve this.
void LinkImpl::AddDocument(StructPtr<Document> doc) {
  FTL_LOG(INFO) << "LinkImpl::AddOne() " << std::hex << (int64_t)shared_
      << DocumentEditor::ToString(doc);
  shared_->doc = std::move(doc);
  for (auto dst : shared_->impls) {
    dst->Notify(this, shared_->doc);
  }
}

}  // modular
