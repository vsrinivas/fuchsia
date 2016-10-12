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

#include <vector>

#include "apps/maxwell/document_store/interfaces/document.mojom.h"
#include "apps/modular/story_runner/link.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/bindings/struct_ptr.h"

namespace modular {

struct SharedLinkImplData;

class LinkImpl : public Link {
 public:
  ~LinkImpl() override;

  // Implements Link interface.
  void AddDocument(mojo::StructPtr<document_store::Document> doc) override;
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

  // For use by the constructor only
  void AddImpl(LinkImpl* client);
  // For use by the destructor only
  void RemoveImpl(LinkImpl* client);

  void AddWatcher(mojo::InterfaceHandle<LinkChanged> watcher, bool self);
  void Notify(LinkImpl* source,
              const mojo::StructPtr<document_store::Document>& doc);

  const bool primary_;
  // |shared_| is owned by the |primary_| LinkImpl.
  SharedLinkImplData* shared_;
  mojo::StrongBinding<Link> binding_;
  // |watchers_| are maintained on a per-handle basis.
  // TODO(jimbe) Need to make this smarter in case watchers close their handles.
  std::vector<std::pair<mojo::InterfacePtr<LinkChanged>, bool>> watchers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LinkImpl);
};

}  // namespace modular

#endif  // MOJO_APPS_MODULAR_STORY_RUNNER_LINK_H__
