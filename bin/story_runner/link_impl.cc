// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/link_impl.h"

#include "apps/modular/lib/document_editor/document_editor.h"
#include "apps/modular/services/document_store/document.fidl.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/services/story/module.fidl.h"
#include "apps/modular/src/story_runner/story_impl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/logging.h"

namespace modular {

using document_store::Document;
using document_store::DocumentPtr;
using document_store::Value;
using document_store::ValuePtr;

using fidl::Array;
using fidl::InterfaceHandle;
using fidl::InterfacePtr;
using fidl::InterfaceRequest;

LinkImpl::LinkImpl(std::shared_ptr<StoryPage> p, const fidl::String& n)
    : name(n), page_(p) {
  // The document map is always valid, even when empty.
  docs_map.mark_non_null();

  FTL_LOG(INFO) << "LinkImpl() " << name;
  page_->MaybeReadLink(name, &docs_map);
}

LinkImpl::~LinkImpl() {
  FTL_LOG(INFO) << "~LinkImpl() " << name;
  page_->WriteLink(name, docs_map);
}

LinkImpl* LinkImpl::New(std::shared_ptr<StoryPage> page,
                        const fidl::String& name,
                        InterfaceRequest<Link> link_request) {
  LinkImpl* const shared = new LinkImpl(page, name);
  new LinkConnection(shared, std::move(link_request));
  return shared;
}

LinkConnection::LinkConnection(LinkImpl* const shared,
                               fidl::InterfaceRequest<Link> link_request)
    : shared_(shared), binding_(this, std::move(link_request)) {
  FTL_LOG(INFO) << "LinkConnection() " << shared->name;
  shared_->impls.emplace_back(this);
  binding_.set_connection_error_handler([this]() { RemoveImpl(); });
}

LinkConnection::~LinkConnection() {
  FTL_LOG(INFO) << "~LinkConnection() " << shared_->name;
}

void LinkConnection::Query(const LinkConnection::QueryCallback& callback) {
  callback(shared_->docs_map.Clone());
}

void LinkConnection::Watch(InterfaceHandle<LinkWatcher> watcher) {
  AddWatcher(std::move(watcher), false);
}

void LinkConnection::WatchAll(InterfaceHandle<LinkWatcher> watcher) {
  AddWatcher(std::move(watcher), true);
}

void LinkConnection::AddWatcher(InterfaceHandle<LinkWatcher> watcher,
                                const bool self_notify) {
  InterfacePtr<LinkWatcher> watcher_ptr;
  watcher_ptr.Bind(std::move(watcher));

  // TODO(jimbe) We need to send an initial notification of state until
  // there is snapshot information that can be used by clients to query the
  // state at this instant. Otherwise there is no sequence information about
  // total state versus incremental changes.
  watcher_ptr->Notify(shared_->docs_map.Clone());

  auto& watcher_set = self_notify ? all_watchers_ : watchers_;
  watcher_set.AddInterfacePtr(std::move(watcher_ptr));
}

void LinkConnection::NotifyWatchers(const FidlDocMap& docs,
                                    const bool self_notify) {
  if (self_notify) {
    watchers_.ForAllPtrs([&docs](LinkWatcher* const link_changed) {
      link_changed->Notify(docs.Clone());
    });
  }
  all_watchers_.ForAllPtrs([&docs](LinkWatcher* const link_changed) {
    link_changed->Notify(docs.Clone());
  });
}

void LinkConnection::DatabaseChanged(const FidlDocMap& docs) {
  for (auto& dst : shared_->impls) {
    bool self_notify = (dst.get() != this);
    dst->NotifyWatchers(docs, self_notify);
  }
}

void LinkConnection::Dup(InterfaceRequest<Link> dup) {
  new LinkConnection(shared_, std::move(dup));
}

void LinkConnection::RemoveImpl() {
  auto it = std::remove_if(shared_->impls.begin(), shared_->impls.end(),
                           [this](const std::unique_ptr<LinkConnection>& p) {
                             return p.get() == this;
                           });
  FTL_DCHECK(it != shared_->impls.end());
  shared_->impls.erase(it, shared_->impls.end());
}

// The |LinkConnection| object knows which client made the call to AddDocument()
// or
// SetAllDocument(), so it notifies either all clients or all other clients,
// depending on whether WatchAll() or Watch() was called, respectively.
//
// TODO(jimbe) This mechanism breaks if the call to Watch() is made *after*
// the call to SetAllDocument(). Need to find a way to improve this.
void LinkConnection::AddDocuments(FidlDocMap mojo_add_docs) {
  DocMap add_docs;
  mojo_add_docs.Swap(&add_docs);

  bool dirty = false;
  for (auto& add_doc : add_docs) {
    DocumentEditor editor;
    if (!editor.Edit(add_doc.first, &shared_->docs_map)) {
      FTL_LOG(INFO) << "LinkImpl::AddDocuments()    docid NEW.";
      // Docid does not currently exist. Add the entire Document.
      shared_->docs_map[add_doc.first] = std::move(add_doc.second);
      dirty = true;
    } else {
      // Docid does exist. Add or update the individual properties.
      FTL_LOG(INFO) << "LinkImpl::AddDocuments()    docid EXISTS.";
      auto& new_props = add_doc.second->properties;
      for (auto it = new_props.begin(); it != new_props.end(); ++it) {
        const std::string& new_key = it.GetKey();
        ValuePtr& new_value = it.GetValue();

        Value* old_value = editor.GetValue(new_key);
        if (!old_value || !new_value->Equals(*old_value)) {
          dirty = true;
          editor.SetProperty(new_key, std::move(new_value));
        }
      }
    }
  }

  if (dirty) {
    DatabaseChanged(shared_->docs_map);
  } else {
    FTL_LOG(INFO) << "LinkImpl::AddDocuments()    Skipped notify, not dirty";
  }
}

void LinkConnection::SetAllDocuments(FidlDocMap new_docs) {
  bool dirty = !new_docs.Equals(shared_->docs_map);
  if (dirty) {
    shared_->docs_map.Swap(&new_docs);
    DatabaseChanged(shared_->docs_map);
  }
}

}  // namespace modular
