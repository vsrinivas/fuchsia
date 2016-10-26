// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_APPS_MODULAR_STORY_RUNNER_LINK_H__
#define MOJO_APPS_MODULAR_STORY_RUNNER_LINK_H__

#include <unordered_map>

#include "apps/document_store/interfaces/document.mojom.h"
#include "apps/modular/document_editor/document_editor.h"
#include "apps/modular/story_runner/link.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_ptr_set.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/struct_ptr.h"

namespace modular {

struct SharedLinkImplData;
class SessionPage;

// A Link is a mutable and observable value shared between modules.
// When a module requests to run more modules using
// Session::StartModule(), a Link instance is associated with each
// such request, i.e. a Link instance is shared between at least two
// modules. The same Link instance can be used in multiple
// StartModule() requests, so it can be shared between more than two
// modules. The Dup() method allows to obtain more handles of the same
// Link instance.
//
// If a watcher is registered through one handle, it only receives
// notifications for changes by requests through other handles. To
// make this possible, each connection is associated with a separate
// implementation instance. All implementation instances share a
// common internal data object that holds the data
// (SharedLinkImplData).
//
// The first such instance is created by SessionImpl::CreateLink()
// using the New() method. Subsequent such instances associated with
// the same shared data are created by LinkImpl::Dup(). The first
// instance is called the primary instance. If the pipe to this
// instance is closed, all other connections are closed too. If a pipe
// to a non-primary instance is closed, only that instance is removed
// from the set of owners of the shared data. This is how it is now,
// it may change in the future.
class LinkImpl : public Link {
 public:
  ~LinkImpl() override;

  // Implements Link interface.
  void AddDocuments(MojoDocMap docs) override;
  void SetAllDocuments(MojoDocMap docs) override;
  void Query(const QueryCallback& callback) override;
  void Watch(mojo::InterfaceHandle<LinkChanged> watcher) override;
  void WatchAll(mojo::InterfaceHandle<LinkChanged> watcher) override;
  void Dup(mojo::InterfaceRequest<Link> dup) override;

  // Connect a new LinkImpl object on the heap. It manages its own lifetime.
  // If this pipe is closed, then everything will be torn down. In comparison,
  // handles created by Dup() do not affect other handles.
  static void New(std::shared_ptr<SessionPage> page,
                  const mojo::String& name,
                  mojo::InterfaceRequest<Link> req);

 private:
  // LinkImpl may not be constructed on the stack, so the constructors
  // are private.

  // Called from New() by outside clients.
  LinkImpl(std::shared_ptr<SessionPage> page, const mojo::String& name,
           mojo::InterfaceRequest<Link> req);

  // Called from Dup().
  LinkImpl(mojo::InterfaceRequest<Link> req, SharedLinkImplData* shared);

  // For use by the binding error handler.
  void RemoveImpl();

  void AddWatcher(mojo::InterfaceHandle<LinkChanged> watcher,
                  const bool self_notify);
  void NotifyWatchers(const MojoDocMap& docs, const bool self_notify);
  void DatabaseChanged(const MojoDocMap& docs);

  // |shared_| is owned (and eventually deleted) by the LinkImpl
  // instance that created it, aka the primary instance.
  SharedLinkImplData* const shared_;
  mojo::Binding<Link> binding_;

  // These watchers do not want self notifications.
  mojo::InterfacePtrSet<LinkChanged> watchers_;
  // These watchers want all notifications.
  mojo::InterfacePtrSet<LinkChanged> all_watchers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LinkImpl);
};

}  // namespace modular

#endif  // MOJO_APPS_MODULAR_STORY_RUNNER_LINK_H__
