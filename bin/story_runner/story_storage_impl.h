// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_STORY_RUNNER_STORY_STORAGE_IMPL_H_
#define APPS_MODULAR_SRC_STORY_RUNNER_STORY_STORAGE_IMPL_H_

#include <string>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/lib/fidl/operation.h"
#include "apps/modular/lib/ledger/ledger_client.h"
#include "apps/modular/lib/ledger/page_client.h"
#include "apps/modular/lib/ledger/types.h"
#include "apps/modular/services/module/module_data.fidl.h"
#include "apps/modular/services/story/link_change.fidl.h"
#include "apps/modular/services/story/per_device_story_info.fidl.h"
#include "apps/modular/services/story/story_data.fidl.h"
#include "apps/modular/services/surface/surface.fidl.h"

namespace modular {

class LinkImpl;
class XdrContext;

void XdrLinkChange(XdrContext* xdr, LinkChange* data);

class LinkStorage {
 public:
  using AllLinkChangeCallback = std::function<void(fidl::Array<LinkChangePtr>)>;
  using DataCallback = std::function<void(const fidl::String&)>;
  using SyncCallback = std::function<void()>;

  virtual ~LinkStorage() = 0;

  virtual void ReadLinkData(const LinkPathPtr& link_path,
                            const DataCallback& callback) = 0;
  virtual void ReadAllLinkData(const LinkPathPtr& link_path,
                               const AllLinkChangeCallback& callback) = 0;
  virtual void WriteLinkData(const LinkPathPtr& link_path,
                             const fidl::String& data,
                             const SyncCallback& callback) = 0;
  virtual void WriteIncrementalLinkData(const LinkPathPtr& link_path,
                                        fidl::String key,
                                        LinkChangePtr link_change,
                                        const SyncCallback& callback) = 0;

  virtual void FlushWatchers(const SyncCallback& callback) = 0;
  virtual void WatchLink(const LinkPathPtr& link_path,
                         LinkImpl* impl,
                         const DataCallback& watcher) = 0;
  virtual void DropWatcher(LinkImpl* impl) = 0;

  virtual void Sync(const SyncCallback& callback) = 0;
};

// A wrapper around a ledger page to store links in a story that runs
// asynchronous operations pertaining to one Story instance in a
// dedicated OperationQueue instance.
class StoryStorageImpl : PageClient, public LinkStorage {
 public:
  using AllModuleDataCallback = std::function<void(fidl::Array<ModuleDataPtr>)>;
  using ModuleDataCallback = std::function<void(ModuleDataPtr)>;
  using DeviceDataCallback =
      std::function<void(PerDeviceStoryInfoPtr per_device)>;
  using LogCallback = std::function<void(fidl::Array<StoryContextLogPtr>)>;

  explicit StoryStorageImpl(LedgerClient* ledger_client, LedgerPageId page_id);
  ~StoryStorageImpl() override;

  // |LinkStorage|
  void ReadLinkData(const LinkPathPtr& link_path,
                    const DataCallback& callback) override;
  // |LinkStorage|
  void ReadAllLinkData(const LinkPathPtr& link_path,
                       const AllLinkChangeCallback& callback) override;

  // |LinkStorage|
  void WriteLinkData(const LinkPathPtr& link_path,
                     const fidl::String& data,
                     const SyncCallback& callback) override;

  // |LinkStorage|
  void WriteIncrementalLinkData(const LinkPathPtr& link_path, fidl::String key,
                                LinkChangePtr link_change,
                                const SyncCallback& callback) override;

  void ReadModuleData(const fidl::Array<fidl::String>& module_path,
                      const ModuleDataCallback& callback);
  void ReadAllModuleData(const AllModuleDataCallback& callback);

  void WriteModuleData(const fidl::Array<fidl::String>& module_path,
                       const fidl::String& module_url,
                       const LinkPathPtr& link_path,
                       ModuleSource module_source,
                       const SurfaceRelationPtr& surface_relation,
                       bool module_stopped,
                       const SyncCallback& callback);
  void WriteModuleData(ModuleDataPtr data, const SyncCallback& callback);

  void WriteDeviceData(const std::string& story_id,
                       const std::string& device_id,
                       StoryState state,
                       const SyncCallback& callback);

  void Log(StoryContextLogPtr log_entry);
  void ReadLog(const LogCallback& callback);

  // |LinkStorage|
  void Sync(const SyncCallback& callback) override;

  // When callback is invoked, all watcher notifications for pending changes are
  // guaranteed to have been received.
  // |LinkStorage|
  void FlushWatchers(const SyncCallback& callback) override;

  // |LinkStorage|
  void WatchLink(const LinkPathPtr& link_path,
                 LinkImpl* impl,
                 const DataCallback& watcher) override;
  // |LinkStorage|
  void DropWatcher(LinkImpl* impl) override;

 private:
  // |PageClient|
  void OnPageChange(const std::string& key, const std::string& value) override;

  // Clients to notify when the value of a given link changes in the
  // ledger page. The first element in the pair is the key for the link path.
  struct WatcherEntry {
    std::string key;
    LinkImpl* impl;  // Not owned.
    DataCallback watcher;
  };
  std::vector<WatcherEntry> watchers_;

  // All asynchronous operations are sequenced by this operation
  // queue.
  OperationQueue operation_queue_;

  // Operations implemented here.
  class ReadLinkDataCall;
  class WriteLinkDataCall;
  class WriteIncrementalLinkDataCall;
  class FlushWatchersCall;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryStorageImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_STORY_STORAGE_IMPL_H_
