// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_USER_RUNNER_STORY_STORAGE_IMPL_H_
#define APPS_MODULAR_SRC_USER_RUNNER_STORY_STORAGE_IMPL_H_

#include <string>
#include <unordered_map>
#include <memory>

#include "apps/modular/services/story/story.fidl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "apps/modular/mojo/strong_binding.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/logging.h"

namespace modular {

// A memory only implementation of storage for story data. We use
// until Ledger doesn't crash anymore;
// https://fuchsia.atlassian.net/browse/LE-46.
class StoryStorageImpl : public StoryStorage {
 public:
  using Storage = std::unordered_map<std::string, StoryDataPtr>;

  StoryStorageImpl(std::shared_ptr<Storage> storage,
                   const fidl::String& key,
                   fidl::InterfaceRequest<StoryStorage> request)
      : binding_(this, std::move(request)), key_(key), storage_(storage) {
    FTL_LOG(INFO) << "StoryStorageImpl() " << key_;
  }

  ~StoryStorageImpl() override {
    FTL_LOG(INFO) << "~StoryStorageImpl() " << key_;
  }

 private:
  void ReadStoryData(const ReadStoryDataCallback& cb) override {
    FTL_LOG(INFO) << "StoryStorageImpl::ReadStoryData() " << key_;

    // story_page_->GetSnapshot(GetProxy(&story_page_snapshot_),
    //                            [](ledger::Status status) {});
    // story_page_snapshot_->Get(
    //     to_array("story_data"),
    //     [this, done](ledger::Status status, ledger::ValuePtr value) {
    //       if (value) {
    //         data_->Deserialize(value->get_bytes().data(),
    //                            value->get_bytes().size());
    //       }
    //       done();
    //     });

    if (storage_->find(key_) != storage_->end()) {
      cb(storage_->find(key_)->second->Clone());
    } else {
      cb(nullptr);
    }
  }

  void WriteStoryData(StoryDataPtr data) override {
    FTL_LOG(INFO) << "StoryStorageImpl::WriteStoryData() " << key_;
    storage_->emplace(std::make_pair(key_, std::move(data)));
  }

  StrongBinding<StoryStorage> binding_;
  const std::string key_;
  std::shared_ptr<Storage> storage_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryStorageImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_USER_RUNNER_STORY_STORAGE_IMPL_H_
