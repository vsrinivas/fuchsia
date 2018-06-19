// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_STORY_RUNNER_LINK_IMPL_H_
#define PERIDOT_BIN_USER_RUNNER_STORY_RUNNER_LINK_IMPL_H_

#include <set>
#include <vector>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>

#include "lib/async/cpp/operation.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/interface_handle.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/interface_ptr_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/user_runner/storage/story_storage.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/types.h"
#include "peridot/lib/rapidjson/rapidjson.h"

using fuchsia::modular::Link;
using fuchsia::modular::LinkPath;
using fuchsia::modular::LinkWatcher;

namespace modular {

class StoryStorage;

// Use the CrtAllocator and not the pool allocator so that merging doesn't
// require deep copying.
using CrtJsonDoc =
    rapidjson::GenericDocument<rapidjson::UTF8<>, rapidjson::CrtAllocator>;
using CrtJsonValue = CrtJsonDoc::ValueType;
using CrtJsonPointer = rapidjson::GenericPointer<CrtJsonValue>;

// A Link is a mutable and observable value that is persistent across story
// restarts, synchronized across devices, and can be shared between modules.
//
// When a module requests to run more modules using
// ModuleContext::StartModule(), one or more Link instances are associated with
// each such request (as specified in the Intent). Link instances can be shared
// between multiple modules. The same Link instance can be used in multiple
// StartModule() requests, so it can be shared between more than two modules.
// Link instances have names that are local to each Module, and can be accessed
// by calling ModuleContext.GetLink(name).
//
// If a watcher is registered through one handle using the Watch() method, it
// only receives notifications for changes by requests through other handles.
class LinkImpl : public Link {
 public:
  // The |link_path| contains the series of module names (where the last
  // element is the module that created this Link) that this Link is namespaced
  // under. If |create_link_info| is null, then this is a request to connect to
  // an existing link.
  LinkImpl(StoryStorage* story_storage, LinkPath link_path);

  ~LinkImpl() override;

  void Set(fidl::VectorPtr<fidl::StringPtr> path,
           fidl::StringPtr json) override;
  void Get(fidl::VectorPtr<fidl::StringPtr> path,
           GetCallback callback) override;
  void Erase(fidl::VectorPtr<fidl::StringPtr> path) override;
  void GetEntity(GetEntityCallback callback) override;
  void SetEntity(fidl::StringPtr entity_reference) override;
  void Watch(fidl::InterfaceHandle<LinkWatcher> watcher) override;
  void WatchAll(fidl::InterfaceHandle<LinkWatcher> watcher) override;
  void Sync(SyncCallback callback) override;

  // Used by StoryControllerImpl;
  const LinkPath& link_path() const { return link_path_; }

 private:
  // Called by |story_storage_|.
  void OnLinkValueChanged(const fidl::StringPtr& value, const void* context);

  fxl::WeakPtr<LinkImpl> GetWeakPtr();

  StoryStorage* const story_storage_;
  const LinkPath link_path_;

  // Bindings as a result from Watch() are stored in |normal_watchers_|, while
  // calls to WatchAll() are stored in |everything_watchers_|. They are separate
  // because Watch() bindings ignore any change notifications that originated
  // on this instance of LinkImpl.
  fidl::InterfacePtrSet<LinkWatcher> normal_watchers_;
  fidl::InterfacePtrSet<LinkWatcher> everything_watchers_;

  fxl::WeakPtrFactory<LinkImpl> weak_factory_;
  StoryStorage::LinkWatcherAutoCancel link_watcher_auto_cancel_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LinkImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_STORY_RUNNER_LINK_IMPL_H_
