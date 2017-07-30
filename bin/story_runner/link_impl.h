// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_STORY_RUNNER_LINK_IMPL_H_
#define APPS_MODULAR_SRC_STORY_RUNNER_LINK_IMPL_H_

#include "apps/modular/lib/fidl/bottleneck.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/services/module/module_data.fidl.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/src/story_runner/story_storage_impl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "third_party/rapidjson/rapidjson/schema.h"

namespace modular {

// Use the CrtAllocator and not the pool allocator so that merging doesn't
// require deep copying.
using CrtJsonDoc =
    rapidjson::GenericDocument<rapidjson::UTF8<>, rapidjson::CrtAllocator>;
using CrtJsonValue = CrtJsonDoc::ValueType;
using CrtJsonPointer = rapidjson::GenericPointer<CrtJsonValue>;

class LinkConnection;
class LinkWatcherConnection;

// A Link is a mutable and observable value shared between modules.
//
// When a module requests to run more modules using
// ModuleContext::StartModule(), a Link instance is associated with each such
// request, i.e. a Link instance is shared between at least two modules. The
// same Link instance can be used in multiple StartModule() requests, so it can
// be shared between more than two modules. The instance is identified by its
// name in the context of the calling module.
//
// If a watcher is registered through one handle using the Watch() method, it
// only receives notifications for changes by requests through other handles. To
// make this possible, each connection is bound to a separate LinkConnection
// instance rather than to LinkImpl directly, and LinkImpl owns all its
// LinkConnection instances.
class LinkImpl {
 public:
  // The |module_path| is the series of module names (where the last element is
  // the module that created this Link) that this Link is namespaced under.
  LinkImpl(StoryStorageImpl* story_storage, const LinkPathPtr& link_path);

  ~LinkImpl();

  // Creates a new LinkConnection for the given request. LinkConnection
  // instances are deleted when their connections close, and they are all
  // deleted and close their connections when LinkImpl is destroyed.
  void Connect(fidl::InterfaceRequest<Link> request);

  // Used by LinkConnection.
  void SetSchema(const fidl::String& json_schema);
  void UpdateObject(fidl::Array<fidl::String> path,
                    const fidl::String& json,
                    LinkConnection* src);
  void Set(fidl::Array<fidl::String> path,
           const fidl::String& json,
           LinkConnection* src);
  void Get(fidl::Array<fidl::String> path,
           const std::function<void(fidl::String)>& callback);
  void Erase(fidl::Array<fidl::String> path, LinkConnection* src);
  void AddConnection(LinkConnection* connection);
  void RemoveConnection(LinkConnection* connection);
  void Sync(const std::function<void()>& callback);
  void Watch(fidl::InterfaceHandle<LinkWatcher> watcher,
             ftl::WeakPtr<LinkConnection> connection);

  // Used by LinkWatcherConnection.
  void RemoveConnection(LinkWatcherConnection* conn);

  // Used by StoryControllerImpl.
  const LinkPathPtr& link_path() const { return link_path_; }
  void set_orphaned_handler(const std::function<void()>& fn) {
    orphaned_handler_ = fn;
  }

 private:
  static bool MergeObject(CrtJsonValue& target,
                          CrtJsonValue&& source,
                          CrtJsonValue::AllocatorType& allocator);

  void DatabaseChanged(LinkConnection* src);
  void NotifyWatchers(LinkConnection* src);
  void ReadLinkData(const std::function<void()>& callback);
  void WriteLinkData(const std::function<void()>& callback);
  void WriteLinkDataImpl(const std::function<void()>& callback);
  void OnChange(const fidl::String& json);
  void ValidateSchema(const char* entry_point,
                      const CrtJsonPointer& pointer,
                      const std::string& json);

  // We can only accept connection requests once the instance is fully
  // initalized. So we queue them up initially.
  bool ready_{};
  std::vector<fidl::InterfaceRequest<Link>> requests_;

  // The value of this Link instance.
  CrtJsonDoc doc_;

  // Fidl connections to this Link instance. We need to explicitly keep track of
  // connections so we can give some watchers only notifications on changes
  // coming from *other* connections than the one the watcher was registered on.
  std::vector<std::unique_ptr<LinkConnection>> connections_;

  // Some watchers do not want notifications for changes made through the
  // connection they were registered on. Therefore, the connection they were
  // registered on is kept associated with them. The connection may still go
  // down before the watcher connection.
  //
  // Some watchers want all notifications, even from changes made through the
  // connection they were registered on. Therefore, they are not associated with
  // a connection, and the connection is recorded as nullptr. These watchers
  // obviously also may survive the connections they were registered on.
  std::vector<std::unique_ptr<LinkWatcherConnection>> watchers_;

  // The hierarchical identifier of this Link instance within its Story.
  const LinkPathPtr link_path_;

  // Link values are stored here.
  StoryStorageImpl* const story_storage_;

  // When the Link instance loses all its Link connections, this callback is
  // invoked. It will cause the Link instance to be deleted. Remaining
  // LinkWatcher connections do not retain the Link instance, but instead can
  // watch it being deleted (through their connection error handler).
  std::function<void()> orphaned_handler_;

  // A JSON schema to be applied to the Link value.
  std::unique_ptr<rapidjson::SchemaDocument> schema_doc_;

  // Helps to defer asynchronous notification of updated values until after they
  // have been written to Ledger, and not have been updated while they were
  // written.
  Bottleneck write_link_data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LinkImpl);
};

class LinkConnection : Link {
 public:
  ~LinkConnection() override;

  // Creates a new instance on the heap and registers it with the
  // given LinkImpl, which takes ownership. It cannot be on the stack
  // because it destroys itself when its fidl connection closes. The
  // constructor is therefore private and only accessible from here.
  static void New(LinkImpl* const impl, fidl::InterfaceRequest<Link> request) {
    new LinkConnection(impl, std::move(request));
  }

 private:
  // Private so it cannot be created on the stack.
  LinkConnection(LinkImpl* impl, fidl::InterfaceRequest<Link> request);

  // |Link|
  void SetSchema(const fidl::String& json_schema) override;
  void UpdateObject(fidl::Array<fidl::String> path,
                    const fidl::String& json) override;
  void Set(fidl::Array<fidl::String> path, const fidl::String& json) override;
  void Get(fidl::Array<fidl::String> path,
           const GetCallback& callback) override;
  void Erase(fidl::Array<fidl::String> path) override;
  void Watch(fidl::InterfaceHandle<LinkWatcher> watcher) override;
  void WatchAll(fidl::InterfaceHandle<LinkWatcher> watcher) override;
  void Sync(const SyncCallback& callback) override;

  LinkImpl* const impl_;
  fidl::Binding<Link> binding_;

  // Weak pointers are used to identify a LinkConnection during notifications of
  // LinkWatchers about value changes, if a LinkWatcher requests to be notified
  // only of changes to the Link value made through other LinkConnections than
  // the one the LinkWatcher was registered through.
  //
  // A weak pointer from this factory is never dereferenced, only compared to
  // the naked pointer of the LinkConnection of an incoming change in order to
  // establish whether a value update is from the same LinkConnection or not.
  //
  // The only reason to use a weak pointer and not a naked pointer is the
  // possibility that a LinkConnection could be deleted, and another
  // LinkConnection instance could be created at the same address as the
  // previous one. During comparison, we must recognize such a new instance
  // as different from the old instance, and a weak pointer to the old instance
  // becomes null in that situation, thus allowing to recognize the instances as
  // different.
  //
  // A value update will never originate from a deleted LinkConnection instance,
  // so deleted LinkConnection instances don't need to be distinguishable from
  // each other, only from non-deleted LinkConnection instances, and a weak
  // nullptr allows to make that distinction too.
  ftl::WeakPtrFactory<LinkConnection> weak_ptr_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LinkConnection);
};

class LinkWatcherConnection {
 public:
  LinkWatcherConnection(LinkImpl* impl,
                        ftl::WeakPtr<LinkConnection> conn,
                        LinkWatcherPtr watcher);
  ~LinkWatcherConnection();

  // Notifies the LinkWatcher in this connection, unless src is the
  // LinkConnection associated with this.
  void Notify(const fidl::String& value, LinkConnection* src);

 private:
  // The LinkImpl this instance belongs to.
  LinkImpl* const impl_;

  // The LinkConnection through which the LinkWatcher was registered. It is a
  // weak pointer because it may be deleted before the LinkWatcher is
  // disconnected.
  ftl::WeakPtr<LinkConnection> conn_;

  LinkWatcherPtr watcher_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LinkWatcherConnection);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_LINK_IMPL_H_
