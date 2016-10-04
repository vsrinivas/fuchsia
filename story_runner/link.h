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

#include "apps/modular/story_runner/link.mojom.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/bindings/struct_ptr.h"
#include "mojo/public/cpp/system/macros.h"

namespace modular {

class LinkImpl;

// LinkHost keeps a single connection from a client to a LinkImpl
// together with pointers to all watchers registered through this
// connection. We need this as a separate class so that we can
// identify where an updated value comes from, so that we are able to
// suppress notifications sent to the same client.
//
// A host can be primary. If it's primary, then it deletes the
// LinkImpl instance that is shared between all connections when it's
// closed, analog to a strong Binding.
class LinkHost : public Link {
 public:
  LinkHost(LinkImpl* impl, mojo::InterfaceRequest<Link> req, bool primary);
  ~LinkHost();

  // Implements Link interface. Forwards to LinkImpl, therefore the
  // methods are implemented below, after LinkImpl is defined.
  void SetValue(mojo::StructPtr<LinkValue> value) override;
  void Value(const ValueCallback& callback) override;
  void Watch(mojo::InterfaceHandle<LinkChanged> watcher) override;
  void WatchAll(mojo::InterfaceHandle<LinkChanged> watcher) override;
  void Dup(mojo::InterfaceRequest<Link> dup) override;

  // Called back from LinkImpl.
  void Notify(LinkHost* source, const mojo::StructPtr<LinkValue>& value);

 private:
  void AddWatcher(mojo::InterfaceHandle<LinkChanged> watcher, bool self);

  LinkImpl* const impl_;
  mojo::StrongBinding<Link> binding_;
  const bool primary_;
  std::vector<std::pair<mojo::InterfacePtr<LinkChanged>, bool>> watchers_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(LinkHost);
};

// The actual implementation of the Link service. Called from LinkHost
// instances above.
class LinkImpl {
 public:
  explicit LinkImpl(mojo::InterfaceRequest<Link> req);
  ~LinkImpl();

  // The methods below are all called from LinkHost.
  void Add(LinkHost* const client);
  void Remove(LinkHost* const client);
  void SetValue(LinkHost* const src, mojo::StructPtr<LinkValue> value);
  const mojo::StructPtr<LinkValue>& Value() const;

 private:
  mojo::StructPtr<LinkValue> value_;
  std::vector<LinkHost*> clients_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(LinkImpl);
};

}  // namespace modular

#endif  // MOJO_APPS_MODULAR_STORY_RUNNER_LINK_H__
