// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/story_runner/link.h"

#include "apps/document_store/interfaces/document.mojom.h"
#include "apps/modular/document_editor/document_editor.h"
#include "apps/modular/services/story/link.mojom.h"
#include "apps/modular/story_runner/session.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"

namespace modular {

using document_store::Document;
using document_store::DocumentPtr;
using document_store::Property;
using document_store::PropertyPtr;
using document_store::Value;

using modular::DocumentEditor;
using modular::MojoDocMap;
using modular::operator<<;

using mojo::Array;
using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;

struct SharedLinkImplData {
  SharedLinkImplData(std::shared_ptr<SessionPage> p, const mojo::String& n)
      : name(n), page_(p) {
    // The document map is always valid, even when empty.
    docs_map.mark_non_null();

    FTL_LOG(INFO) << "SharedLinkImplData() " << name;
    page_->MaybeReadLink(name, &docs_map);
  }

  ~SharedLinkImplData() {
    FTL_LOG(INFO) << "~SharedLinkImplData() " << name;
    page_->WriteLink(name, docs_map);
  }

  MojoDocMap docs_map;
  std::vector<std::unique_ptr<LinkImpl>> impls;
  const mojo::String name;

 private:
  std::shared_ptr<SessionPage> page_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(SharedLinkImplData);
};

namespace {

using DocIndex =
    std::map<std::pair<const std::string&, const std::string&>, Value*>;

DocIndex IndexDocIdToDocMap(const MojoDocMap& docs_map) {
  DocIndex index;
  for (auto doc_it = docs_map.cbegin(); doc_it != docs_map.cend(); ++doc_it) {
    Array<PropertyPtr>& props =
        const_cast<Array<PropertyPtr>&>(doc_it.GetValue()->properties);

    for (auto& p : props) {
      index[std::make_pair(doc_it.GetKey(), p->property)] = p->value.get();
    }
  }

  return index;
}

bool Equal(const MojoDocMap& docs_map1, const MojoDocMap& docs_map2) {
  if (docs_map1.size() != docs_map2.size()) {
    return false;
  }

  DocIndex index1 = IndexDocIdToDocMap(docs_map1);
  DocIndex index2 = IndexDocIdToDocMap(docs_map2);

  return std::equal(
      index1.begin(), index1.end(), index2.begin(),
      [](const DocIndex::value_type& p1, const DocIndex::value_type& p2) {
        return p1.first == p2.first && p1.second->Equals(*p2.second);
      });
}

}  // namespace

LinkImpl::LinkImpl(std::shared_ptr<SessionPage> page,
                   const mojo::String& name,
                   InterfaceRequest<Link> req)
    : shared_(new SharedLinkImplData(page, name)),
      binding_(this, std::move(req)) {
  FTL_LOG(INFO) << "LinkImpl() " << name << " (primary) ";

  shared_->impls.emplace_back(this);

  // If the primary connection goes down, the whole implementation is
  // deleted, taking down all remaining connections. This corresponds
  // to a strong binding on the first connection, and regular bindings
  // on all later ones. This is just how it is and may be revised in
  // the future.
  binding_.set_connection_error_handler([this]() { delete shared_; });
}

LinkImpl::LinkImpl(InterfaceRequest<Link> req, SharedLinkImplData* const shared)
    : shared_(shared), binding_(this, std::move(req)) {
  FTL_LOG(INFO) << "LinkImpl() " << shared->name;

  shared_->impls.emplace_back(this);
  binding_.set_connection_error_handler([this]() { RemoveImpl(); });
}

LinkImpl::~LinkImpl() {
  FTL_LOG(INFO) << "~LinkImpl() " << shared_->name;
}

void LinkImpl::New(std::shared_ptr<SessionPage> page,
                   const mojo::String& name,
                   InterfaceRequest<Link> req) {
  new LinkImpl(page, name, std::move(req));
}

void LinkImpl::Query(const LinkImpl::QueryCallback& callback) {
  callback.Run(shared_->docs_map.Clone());
}

void LinkImpl::Watch(InterfaceHandle<LinkChanged> watcher) {
  AddWatcher(std::move(watcher), false);
}

void LinkImpl::WatchAll(InterfaceHandle<LinkChanged> watcher) {
  AddWatcher(std::move(watcher), true);
}

void LinkImpl::AddWatcher(InterfaceHandle<LinkChanged> watcher,
                          const bool self_notify) {
  InterfacePtr<LinkChanged> watcher_ptr;
  watcher_ptr.Bind(std::move(watcher));

  // TODO(jimbe) We need to send an initial notification of state until
  // there is snapshot information that can be used by clients to query the
  // state at this instant. Otherwise there is no sequence information about
  // total state versus incremental changes.
  watcher_ptr->Notify(shared_->docs_map.Clone());

  auto& watcher_set = self_notify ? all_watchers_ : watchers_;
  watcher_set.AddInterfacePtr(std::move(watcher_ptr));
}

void LinkImpl::NotifyWatchers(const MojoDocMap& docs, const bool self_notify) {
  if (self_notify) {
    watchers_.ForAllPtrs([&docs](LinkChanged* const link_changed) {
      link_changed->Notify(docs.Clone());
    });
  }
  all_watchers_.ForAllPtrs([&docs](LinkChanged* const link_changed) {
    link_changed->Notify(docs.Clone());
  });
}

void LinkImpl::DatabaseChanged(const MojoDocMap& docs) {
  for (auto& dst : shared_->impls) {
    bool self_notify = (dst.get() != this);
    dst->NotifyWatchers(docs, self_notify);
  }
}

void LinkImpl::Dup(InterfaceRequest<Link> dup) {
  new LinkImpl(std::move(dup), shared_);
}

void LinkImpl::RemoveImpl() {
  auto it = std::remove_if(
      shared_->impls.begin(), shared_->impls.end(),
      [this](const std::unique_ptr<LinkImpl>& p) { return p.get() == this; });
  FTL_DCHECK(it != shared_->impls.end());
  shared_->impls.erase(it, shared_->impls.end());
}

// The |LinkImpl| object knows which client made the call to AddDocument() or
// SetAllDocument(), so it notifies either all clients or all other clients,
// depending on whether WatchAll() or Watch() was called, respectively.
//
// TODO(jimbe) This mechanism breaks if the call to Watch() is made *after*
// the call to SetAllDocument(). Need to find a way to improve this.
void LinkImpl::AddDocuments(MojoDocMap mojo_add_docs) {
  FTL_LOG(INFO) << "LinkImpl::AddDocuments() " << shared_->name << " "
                << mojo_add_docs;
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
      for (auto& p : add_doc.second->properties) {
        Value* v = editor.GetValue(p->property);
        if (!v || !v->Equals(*p->value)) {
          dirty = true;
          editor.SetProperty(std::move(p));
        }
      }
      editor.TakeDocument(&shared_->docs_map[editor.docid()]);
    }
  }

  if (dirty) {
    DatabaseChanged(shared_->docs_map);
  } else {
    FTL_LOG(INFO) << "LinkImpl::AddDocuments() Skipped notify, not dirty";
  }
}

void LinkImpl::SetAllDocuments(MojoDocMap new_docs) {
  FTL_LOG(INFO) << "LinkImpl::SetAllDocuments() " << shared_->name << " "
                << new_docs;

  bool dirty = !Equal(new_docs, shared_->docs_map);
  if (dirty) {
    shared_->docs_map.Swap(&new_docs);
    DatabaseChanged(shared_->docs_map);
  } else {
    FTL_LOG(INFO) << "LinkImpl::SetAllDocuments() Skipped notify, not dirty";
  }
}

}  // namespace modular
