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
#include "third_party/rapidjson/rapidjson/schema.h"

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
  // Connects a new LinkConnection object for the given Link interface
  // request. The |module_path| is the series of module names (where
  // the last element is the module that created this Link) that this
  // Link is namespaced under.
  LinkImpl(StoryStorageImpl* story_storage, const LinkPathPtr& link_path);

  ~LinkImpl();

  // Creates a new LinkConnection for the given request.
  // LinkConnection instances are deleted when their connections
  // close, and they are all deleted and close their connections when
  // LinkImpl is destroyed.
  void Connect(fidl::InterfaceRequest<Link> request);

  // Used by LinkConnection.
  void SetSchema(const fidl::String& json_schema);
  void UpdateObject(fidl::Array<fidl::String> path,
                    const fidl::String& json,
                    LinkConnection* src);
  void Set(fidl::Array<fidl::String> path,
           const fidl::String& json,
           LinkConnection* src);
  void Erase(fidl::Array<fidl::String> path, LinkConnection* src);
  void AddConnection(LinkConnection* connection);
  void RemoveConnection(LinkConnection* connection);
  const CrtJsonDoc& doc() const { return doc_; }
  void Sync(const std::function<void()>& callback);

  // Used by StoryControllerImpl.
  const LinkPathPtr& link_path() const { return link_path_; }
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
  void ValidateSchema(const char* entry_point,
                      const CrtJsonPointer& pointer,
                      const std::string& json);

  // We can only accept connection requests once the instance is fully
  // initalized. So we queue them up initially.
  bool ready_{};
  std::vector<fidl::InterfaceRequest<Link>> requests_;

  CrtJsonDoc doc_;
  std::vector<std::unique_ptr<LinkConnection>> connections_;
  const LinkPathPtr link_path_;
  StoryStorageImpl* const story_storage_;
  std::function<void()> orphaned_handler_;

  std::unique_ptr<rapidjson::SchemaDocument> schema_doc_;

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

  void NotifyWatchers(const CrtJsonDoc& doc, const bool self_notify);

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
