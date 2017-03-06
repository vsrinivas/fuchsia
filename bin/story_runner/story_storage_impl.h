// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_STORY_RUNNER_STORY_STORAGE_IMPL_H_
#define APPS_MODULAR_SRC_STORY_RUNNER_STORY_STORAGE_IMPL_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/lib/fidl/operation.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"

namespace modular {

// An optionally memory only implementation of storage for story data.
// If |story_page| is not bound, story data are just kept in memory.
// This is useful when ledger is broken. TODO(mesch): Eventually
// in-memory storage can be removed from here.
class StoryStorageImpl : public ledger::PageWatcher {
 public:
  using Storage =
      std::unordered_map<std::string,
                         std::unordered_map<std::string, std::string>>;

  using DataCallback = std::function<void(const fidl::String&)>;

  using SyncCallback = std::function<void()>;

  StoryStorageImpl(std::shared_ptr<Storage> storage,
                   ledger::PagePtr story_page,
                   const fidl::String& key);

  ~StoryStorageImpl() override;

  void ReadLinkData(const fidl::String& link_id, const DataCallback& callback);
  void WriteLinkData(const fidl::String& link_id,
                     const fidl::String& data,
                     const SyncCallback& callback);
  void WatchLink(const fidl::String& link_id, const DataCallback& watcher);
  void Sync(const SyncCallback& callback);

 private:
  // |PageWatcher|
  void OnChange(ledger::PageChangePtr page,
                const OnChangeCallback& callback) override;

  // Every time we receive an OnChange notification, we update the
  // story page snapshot so we see the current state. Just in case, we
  // also install a connection error handler on the snapshot
  // connection, so we can log when the connection unexepctedly closes
  // (although we cannot do anything else about it).
  fidl::InterfaceRequest<ledger::PageSnapshot> ResetStorySnapshot();

  fidl::Binding<ledger::PageWatcher> page_watcher_binding_;
  std::vector<std::pair<fidl::String, DataCallback>> watchers_;
  const std::string key_;
  std::shared_ptr<Storage> storage_;

  // The page we store the story data in.
  ledger::PagePtr story_page_;

  // The current snapshot of the page we read from, as obtained from
  // the watcher on the story page. It is held by a shared pointer,
  // because we may update it while Operation instances that read from
  // it are still in progress, and they need to hold on to the same
  // snapshot they started with, lest the methods called on that
  // snapshot never return. PageSnaphotPtr cannot be duplicated,
  // because it doesn't have a duplicate method.
  std::shared_ptr<ledger::PageSnapshotPtr> story_snapshot_;

  OperationQueue operation_queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryStorageImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_STORY_STORAGE_IMPL_H_
