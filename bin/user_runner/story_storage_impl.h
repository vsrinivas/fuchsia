// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_USER_RUNNER_STORY_STORAGE_IMPL_H_
#define APPS_MODULAR_SRC_USER_RUNNER_STORY_STORAGE_IMPL_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/modular/lib/fidl/strong_binding.h"
#include "apps/modular/services/story/story_storage.fidl.h"
#include "apps/modular/src/user_runner/transaction.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"

namespace modular {

// An optionally memory only implementation of storage for story data.
// If |story_page| is not bound, story data are just kept in memory.
// This is useful when ledger is broken. TODO(mesch): Eventually
// in-memory storage can be removed from here.
class StoryStorageImpl : public StoryStorage {
 public:
  using Storage = std::unordered_map<std::string, StoryDataPtr>;

  StoryStorageImpl(std::shared_ptr<Storage> storage,
                   ledger::PagePtr story_page,
                   const fidl::String& key,
                   fidl::InterfaceRequest<StoryStorage> request);

  ~StoryStorageImpl() override;

 private:
  void ReadStoryData(const ReadStoryDataCallback& cb) override;
  void WriteStoryData(StoryDataPtr data) override;

  StrongBinding<StoryStorage> binding_;
  const std::string key_;
  std::shared_ptr<Storage> storage_;
  ledger::PagePtr story_page_;
  TransactionContainer transaction_container_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryStorageImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_USER_RUNNER_STORY_STORAGE_IMPL_H_
