// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
// implementation instance, called a host.

#ifndef MOJO_APPS_MODULAR_STORY_RUNNER_LINK_H__
#define MOJO_APPS_MODULAR_STORY_RUNNER_LINK_H__

#include <unordered_map>

#include "apps/document_store/interfaces/document.mojom.h"
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

class LinkImpl : public Link {
 public:
  ~LinkImpl() override;

  // Implements Link interface.
  void AddDocuments(mojo::Array<document_store::DocumentPtr> docs) override;
  void SetAllDocuments(mojo::Array<document_store::DocumentPtr> docs) override;
  void Query(const QueryCallback& callback) override;
  void Watch(mojo::InterfaceHandle<LinkChanged> watcher) override;
  void WatchAll(mojo::InterfaceHandle<LinkChanged> watcher) override;
  void Dup(mojo::InterfaceRequest<Link> dup) override;

  // Connect a new LinkImpl object on the heap. It manages its own lifetime.
  // If this pipe is closed, then everything will be torn down. In comparison,
  // handles created by Dup() do not affect other handles.
  static void New(mojo::InterfaceRequest<Link> req);

 private:
  // LinkImpl may not be constructed on the stack.
  LinkImpl(mojo::InterfaceRequest<Link> req, SharedLinkImplData* shared);

  // For use by the destructor only
  void RemoveImpl(LinkImpl* client);

  void AddWatcher(mojo::InterfaceHandle<LinkChanged> watcher, bool self_notify);
  void NotifyWatchers(const mojo::Array<document_store::DocumentPtr>& docs,
                      bool self_notify);
  void DatabaseChanged(const mojo::Array<document_store::DocumentPtr>& docs);

  // |shared_| is owned by the LinkImpl that called "new SharedLinkImplData()".
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
