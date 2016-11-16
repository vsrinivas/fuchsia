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
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"

namespace modular {

using document_store::Document;
using document_store::DocumentPtr;
using document_store::Value;
using document_store::ValuePtr;

using fidl::Array;
using fidl::InterfaceHandle;
using fidl::InterfacePtr;
using fidl::InterfaceRequest;

LinkImpl::LinkImpl(StoryStoragePtr story_storage,
                   const fidl::String& n,
                   fidl::InterfaceRequest<Link> link_request)
    : name(n), story_storage_(std::move(story_storage)) {
  ReadLinkData(ftl::MakeCopyable([
    this, link_request = std::move(link_request)
  ]() mutable { new LinkConnection(this, std::move(link_request)); }));
}

void LinkImpl::ReadLinkData(const std::function<void()>& done) {
  story_storage_->ReadLinkData(name, [this, done](LinkDataPtr data) {
    if (!data.is_null()) {
      FTL_DCHECK(!data->docs.is_null());
      docs_map = std::move(data->docs);
    } else {
      // The document map is always valid, even when empty.
      docs_map.mark_non_null();
    }

    done();
  });
}

void LinkImpl::WriteLinkData(const std::function<void()>& done) {
  auto link_data = LinkData::New();
  link_data->docs = docs_map.Clone();
  story_storage_->WriteLinkData(name, std::move(link_data), done);
}

void LinkImpl::DatabaseChanged(LinkConnection* const src) {
  // src is only used to compare its value. If the connection was
  // deleted before the callback is invoked, it will also be removed
  // from impls.
  WriteLinkData([this, src]() { NotifyWatchers(src); });
}

void LinkImpl::OnChange(LinkDataPtr link_data) {
  if (docs_map.Equals(link_data->docs)) {
    return;
  }
  docs_map = std::move(link_data->docs);
  NotifyWatchers(nullptr);
}

void LinkImpl::NotifyWatchers(LinkConnection* const src) {
  for (auto& dst : impls) {
    const bool self_notify = (dst.get() != src);
    dst->NotifyWatchers(docs_map, self_notify);
  }
}

LinkConnection::LinkConnection(LinkImpl* const shared,
                               fidl::InterfaceRequest<Link> link_request)
    : shared_(shared), binding_(this, std::move(link_request)) {
  shared_->impls.emplace_back(this);
  binding_.set_connection_error_handler([this]() { RemoveImpl(); });
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
    watchers_.ForAllPtrs(
        [&docs](LinkWatcher* const watcher) { watcher->Notify(docs.Clone()); });
  }
  all_watchers_.ForAllPtrs(
      [&docs](LinkWatcher* const watcher) { watcher->Notify(docs.Clone()); });
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

// The |LinkConnection| object knows which client made the call to
// AddDocument() or SetAllDocument(), so it notifies either all
// clients or all other clients, depending on whether WatchAll() or
// Watch() was called, respectively.
//
// TODO(jimbe) This mechanism breaks if the call to Watch() is made
// *after* the call to SetAllDocument(). Need to find a way to improve
// this.
void LinkConnection::AddDocuments(FidlDocMap mojo_add_docs) {
  DocMap add_docs;
  mojo_add_docs.Swap(&add_docs);

  bool dirty = false;
  for (auto& add_doc : add_docs) {
    DocumentEditor editor;
    if (!editor.Edit(add_doc.first, &shared_->docs_map)) {
      // Docid does not currently exist. Add the entire Document.
      shared_->docs_map[add_doc.first] = std::move(add_doc.second);
      dirty = true;
    } else {
      // Docid does exist. Add or update the individual properties.
      auto& new_props = add_doc.second->properties;
      for (auto it = new_props.begin(); it != new_props.end(); ++it) {
        const std::string& new_key = it.GetKey();
        ValuePtr& new_value = it.GetValue();

        Value* const old_value = editor.GetValue(new_key);
        if (!old_value || !new_value->Equals(*old_value)) {
          dirty = true;
          editor.SetProperty(new_key, std::move(new_value));
        }
      }
    }
  }

  if (dirty) {
    shared_->DatabaseChanged(this);
  }
}

void LinkConnection::SetAllDocuments(FidlDocMap new_docs) {
  bool dirty = !new_docs.Equals(shared_->docs_map);
  if (dirty) {
    shared_->docs_map.Swap(&new_docs);
    shared_->DatabaseChanged(this);
  }
}

}  // namespace modular
