// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_APPS_MODULAR_SRC_STORY_RUNNER_LINK_IMPL_H_
#define MOJO_APPS_MODULAR_SRC_STORY_RUNNER_LINK_IMPL_H_

#include <memory>
#include <unordered_map>

#include "apps/modular/lib/document_editor/document_editor.h"
#include "apps/modular/services/document_store/document.fidl.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/services/story/story_storage.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/ftl/macros.h"

namespace modular {

class LinkConnection;

// A Link is a mutable and observable value shared between modules.
//
// When a module requests to run more modules using
// Story::StartModule(), a Link instance is associated with each such
// request, i.e. a Link instance is shared between at least two
// modules. The same Link instance can be used in multiple
// StartModule() requests, so it can be shared between more than two
// modules. The Dup() method allows to obtain more handles of the same
// Link instance.
//
// If a watcher is registered through one handle, it only receives
// notifications for changes by requests through other handles. To
// make this possible, each connection is associated with a separate
// LinkConnection implementation instance. All implementation
// instances share a common LinkImpl instance that holds the data.
//
// The first such instance is created by StoryImpl::CreateLink() using
// the LinkImpl::New() method. Subsequent such instances associated
// with the same shared data are created by LinkConnection::Dup(). The
// LinkImpl::New() method returns a handle to the shared LinkImpl
// instance. When that instance is deleted, all connections to it are
// closed. This is done by the StoryImpl that created it. TODO(mesch):
// Link instances should already be deleted earlier, when they lose
// all their references.
class LinkImpl : public StoryStorageLinkWatcher {
 public:
  // Connects a new LinkConnection object on the heap for the given
  // Link interface request. LinkImpl owns the LinkConnection created
  // now and all future ones created by Dup(). LinkConnection
  // instances are deleted when their connections close, and they are
  // all deleted (and close their connections) when LinkImpl is
  // destroyed.
  LinkImpl(StoryStoragePtr story_storage,
           const fidl::String& n,
           fidl::InterfaceRequest<Link> link_request);
  ~LinkImpl() override = default;

  void ReadLinkData(const std::function<void()>& done);
  void WriteLinkData(const std::function<void()>& done);
  void DatabaseChanged(LinkConnection* src);

 private:
  friend class LinkConnection;

  // |StoryStorageLinkWatcher|
  void OnChange(LinkDataPtr link_data) override;

  void NotifyWatchers(LinkConnection* src);

  FidlDocMap docs_map;
  std::vector<std::unique_ptr<LinkConnection>> impls;
  const fidl::String name;
  StoryStoragePtr story_storage_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LinkImpl);
};

class LinkConnection : public Link {
 public:
  ~LinkConnection() override = default;

 private:
  // |Link|
  void AddDocuments(FidlDocMap docs) override;
  void SetAllDocuments(FidlDocMap docs) override;
  void Query(const QueryCallback& callback) override;
  void Watch(fidl::InterfaceHandle<LinkWatcher> watcher) override;
  void WatchAll(fidl::InterfaceHandle<LinkWatcher> watcher) override;
  void Dup(fidl::InterfaceRequest<Link> dup) override;

  friend class LinkImpl;

  // LinkConnection may not be constructed on the stack, so the
  // constructor is private. It's used only by LinkImpl.
  LinkConnection(LinkImpl* shared, fidl::InterfaceRequest<Link> link_request);

  void RemoveImpl();

  void AddWatcher(fidl::InterfaceHandle<LinkWatcher> watcher,
                  const bool self_notify);

  // Used by LinkImpl.
  void NotifyWatchers(const FidlDocMap& docs, const bool self_notify);

  LinkImpl* const shared_;
  fidl::Binding<Link> binding_;

  // These watchers do not want self notifications.
  fidl::InterfacePtrSet<LinkWatcher> watchers_;
  // These watchers want all notifications.
  fidl::InterfacePtrSet<LinkWatcher> all_watchers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LinkConnection);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_LINK_IMPL_H_
