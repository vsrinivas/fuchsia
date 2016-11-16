// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_USER_RUNNER_STORY_STORAGE_IMPL_H_
#define APPS_MODULAR_SRC_USER_RUNNER_STORY_STORAGE_IMPL_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/modular/services/story/story_storage.fidl.h"
#include "apps/modular/src/user_runner/transaction.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"

namespace modular {

// An optionally memory only implementation of storage for story data.
// If |story_page| is not bound, story data are just kept in memory.
// This is useful when ledger is broken. TODO(mesch): Eventually
// in-memory storage can be removed from here.
class StoryStorageImpl : public StoryStorage, public ledger::PageWatcher {
 public:
  using Storage =
      std::unordered_map<std::string,
                         std::unordered_map<std::string, LinkDataPtr>>;

  StoryStorageImpl(std::shared_ptr<Storage> storage,
                   ledger::PagePtr story_page,
                   const fidl::String& key,
                   fidl::InterfaceRequest<StoryStorage> request);

  ~StoryStorageImpl() override = default;

 private:
  // |StoryStorage|
  void ReadLinkData(const fidl::String& link_id,
                    const ReadLinkDataCallback& cb) override;
  void WriteLinkData(const fidl::String& link_id,
                     LinkDataPtr data,
                     const WriteLinkDataCallback& cb) override;
  void WatchLink(
      const fidl::String& link_id,
      fidl::InterfaceHandle<StoryStorageLinkWatcher> watcher) override;
  void Dup(fidl::InterfaceRequest<StoryStorage> dup) override;

  // |PageWatcher|
  void OnInitialState(fidl::InterfaceHandle<ledger::PageSnapshot> page,
                      const OnInitialStateCallback& cb) override;
  void OnChange(ledger::PageChangePtr page,
                const OnChangeCallback& cb) override;

  fidl::BindingSet<StoryStorage> bindings_;
  fidl::Binding<ledger::PageWatcher> page_watcher_binding_;
  std::vector<std::pair<fidl::String, StoryStorageLinkWatcherPtr>> watchers_;
  const std::string key_;
  std::shared_ptr<Storage> storage_;
  ledger::PagePtr story_page_;
  TransactionContainer transaction_container_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryStorageImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_USER_RUNNER_STORY_STORAGE_IMPL_H_
