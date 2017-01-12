// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_APPS_MODULAR_SRC_STORY_RUNNER_LINK_IMPL_H_
#define MOJO_APPS_MODULAR_SRC_STORY_RUNNER_LINK_IMPL_H_

#include "apps/modular/lib/fidl/bottleneck.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/src/user_runner/story_storage_impl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace modular {

// Use the CrtAllocator and not the pool allocator so that merging doesn't
// require deep copying.
using CrtJsonDoc =
    rapidjson::GenericDocument<rapidjson::UTF8<>, rapidjson::CrtAllocator>;
using CrtJsonValue = CrtJsonDoc::ValueType;
using CrtJsonPointer = rapidjson::GenericPointer<CrtJsonValue>;

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
class LinkImpl {
 public:
  // Connects a new LinkConnection object on the heap for the given
  // Link interface request. LinkImpl owns the LinkConnection created
  // now and all future ones created by Dup(). LinkConnection
  // instances are deleted when their connections close, and they are
  // all deleted (and close their connections) when LinkImpl is
  // destroyed.
  LinkImpl(StoryStorageImpl* story_storage,
           const fidl::String& name,
           fidl::InterfaceRequest<Link> request);

  ~LinkImpl();

  // Used by LinkConnection.
  void UpdateObject(const fidl::String& path,
                    const fidl::String& json,
                    LinkConnection* src);
  void Set(const fidl::String& path,
           const fidl::String& json,
           LinkConnection* src);
  void Erase(const fidl::String& path, LinkConnection* src);
  void AddConnection(LinkConnection* connection);
  void RemoveConnection(LinkConnection* connection);
  const CrtJsonDoc& doc() const { return doc_; }
  void Sync(const std::function<void()>& callback);

  // Used by StoryImpl.
  void set_orphaned_handler(const std::function<void()>& fn) {
    orphaned_handler_ = fn;
  }

 private:
  bool MergeObject(CrtJsonValue& target,
                   CrtJsonValue&& source,
                   CrtJsonValue::AllocatorType& allocator);

  void DatabaseChanged(LinkConnection* src);
  void NotifyWatchers(LinkConnection* src);
  void ReadLinkData(const std::function<void()>& callback);
  void WriteLinkData(const std::function<void()>& callback);
  void WriteLinkDataImpl(const std::function<void()>& callback);
  void OnChange(const fidl::String& json);

  CrtJsonDoc doc_;
  std::vector<std::unique_ptr<LinkConnection>> connections_;
  const fidl::String name_;
  StoryStorageImpl* const story_storage_;
  std::function<void()> orphaned_handler_;

  Bottleneck write_link_data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LinkImpl);
};

class LinkConnection : public Link {
 public:
  ~LinkConnection() override;

  // Creates a new instance on the heap and registers it with the
  // given LinkImpl, which takes ownership. It cannot be on the stack
  // because it destroys itself when its fidl connection closes. The
  // constructor is therefore private and only accessible from here.
  static void New(LinkImpl* const impl, fidl::InterfaceRequest<Link> request) {
    new LinkConnection(impl, std::move(request));
  }

  void NotifyWatchers(const CrtJsonDoc& doc, const bool self_notify);

 private:
  // Private so it cannot be created on the stack.
  LinkConnection(LinkImpl* impl, fidl::InterfaceRequest<Link> request);

  // |Link|
  void UpdateObject(const fidl::String& path,
                    const fidl::String& json) override;
  void Set(const fidl::String& path, const fidl::String& json) override;
  void Get(const fidl::String& path, const GetCallback& callback) override;
  void Erase(const fidl::String& path) override;
  void Watch(fidl::InterfaceHandle<LinkWatcher> watcher) override;
  void WatchAll(fidl::InterfaceHandle<LinkWatcher> watcher) override;
  void Dup(fidl::InterfaceRequest<Link> dup) override;
  void Sync(const SyncCallback& callback) override;

  // Used by Watch() and WatchAll().
  void AddWatcher(fidl::InterfaceHandle<LinkWatcher> watcher,
                  const bool self_notify);

  LinkImpl* const impl_;
  fidl::Binding<Link> binding_;

  // These watchers do not want self notifications.
  fidl::InterfacePtrSet<LinkWatcher> watchers_;
  // These watchers want all notifications.
  fidl::InterfacePtrSet<LinkWatcher> all_watchers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LinkConnection);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_LINK_IMPL_H_
