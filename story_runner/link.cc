// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "apps/document_store/interfaces/document.mojom.h"
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
using mojo::InterfacePtrSet;
using mojo::InterfaceRequest;
using mojo::StructPtr;

struct SharedLinkImplData {
  StructPtr<Document> doc;
  std::vector<std::unique_ptr<LinkImpl>> impls;
};

LinkImpl::LinkImpl(InterfaceRequest<Link> req, SharedLinkImplData* shared)
    : shared_(shared ? shared : new SharedLinkImplData()),
      binding_(this, std::move(req)) {
  FTL_LOG(INFO) << "LinkImpl()" << (shared == nullptr ? " primary" : "") << std::hex << (int64_t)this;

  shared_->impls.emplace_back(this);
  bool primary = shared == nullptr;

  binding_.set_connection_error_handler([primary, this]() {
    // If a "primary" (currently that's the first) connection goes down,
    // the whole implementation is deleted, taking down all remaining
    // connections. This corresponds to a strong binding on the first
    // connection, and regular bindings on all later ones. This is just
    // how it is and may be revised in the future.
    // "shared" is nullptr for all copies of LinkImpl created by Dup().
    if (primary) {
      delete shared_;
    } else {
      RemoveImpl(this);
    }
  });
}

LinkImpl::~LinkImpl() {
  FTL_LOG(INFO) << "~LinkImpl() " << std::hex << (int64_t)this;
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
                          bool self_notify) {
  InterfacePtr<LinkChanged> watcher_ptr;
  watcher_ptr.Bind(watcher.Pass());

  // The current Document is sent to a newly registered watcher only if
  // it's not null.
  // TODO(jimbe) Sending an initial notification to the watcher smells wrong.
  if (!shared_->doc.is_null() && !shared_->doc->properties.is_null()) {
    watcher_ptr->Notify(shared_->doc.Clone());
  }

  auto& watcher_set = self_notify ?  all_watchers_ : watchers_;
  watcher_set.AddInterfacePtr(std::move(watcher_ptr));
}

void LinkImpl::NotifyWatchers(const StructPtr<Document>& doc, bool self_notify) {
  if (self_notify) {
    watchers_.ForAllPtrs([&doc] (LinkChanged* link_changed) {
      link_changed->Notify(doc.Clone());
    });
  }
  all_watchers_.ForAllPtrs([&doc] (LinkChanged* link_changed) {
    link_changed->Notify(doc.Clone());
  });
}

void LinkImpl::DatabaseChanged(StructPtr<Document>& doc) {
  for (auto& dst : shared_->impls) {
    bool self_notify = (dst.get() != this);
    dst->NotifyWatchers(doc, self_notify);
  }
}

void LinkImpl::Dup(InterfaceRequest<Link> dup) {
  new LinkImpl(std::move(dup), shared_);
}

void LinkImpl::RemoveImpl(LinkImpl* impl) {
  auto it = std::remove_if(shared_->impls.begin(), shared_->impls.end(),
                         [impl](const std::unique_ptr<LinkImpl>& p) {
                           return (p.get() == impl);
                         });
  FTL_DCHECK(it != shared_->impls.end());
  shared_->impls.erase(it, shared_->impls.end());
}

// The |LinkImpl| object knows which client made the call to AddDocument(), so
// it notifies either all clients or all other clients, depending on whether
// WatchAll() or Watch() was called, respectively.
// TODO(jimbe) This mechanism breaks if the call to Watch() is made *after*
// the call to AddDocument(). Need to find a way to improve this.
void LinkImpl::AddDocument(StructPtr<Document> doc) {
  FTL_LOG(INFO) << "LinkImpl::AddDocument() " << std::hex << (int64_t)shared_
      << DocumentEditor::ToString(doc);

  shared_->doc = std::move(doc);
  DatabaseChanged(shared_->doc);
}

}  // modular
