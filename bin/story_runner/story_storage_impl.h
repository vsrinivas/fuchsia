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
#include "apps/modular/src/story_runner/page_snapshot.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"

namespace modular {

// A wrapper around a ledger page to store links in a story that runs
// asynchronous operations pertaining to one Story instance in a
// dedicated OperationQueue instance.
class StoryStorageImpl : ledger::PageWatcher {
 public:
  using DataCallback = std::function<void(const fidl::String&)>;
  using SyncCallback = std::function<void()>;

  StoryStorageImpl(ledger::PagePtr story_page);
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
                ledger::ResultState result_state,
                const OnChangeCallback& callback) override;

  // This instance is a watcher on the ledger Page it stores data in.
  fidl::Binding<ledger::PageWatcher> page_watcher_binding_;

  // Clients to notify when the value of a given link changes in the
  // ledger page. The first element in the pair is the link ID.
  std::vector<std::pair<fidl::String, DataCallback>> watchers_;

  // The ledger page the story data is stored in.
  ledger::PagePtr story_page_;

  // The current snapshot of the page obtained by watching it.
  PageSnapshot story_snapshot_;

  // All asynchronous operations are sequenced by this operation
  // queue.
  OperationQueue operation_queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryStorageImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_STORY_STORAGE_IMPL_H_
