// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_STORY_MANAGER_SESSION_STORAGE_IMPL_H_
#define APPS_MODULAR_STORY_MANAGER_SESSION_STORAGE_IMPL_H_

#include <string>
#include <unordered_map>
#include <memory>

#include "apps/modular/services/story/session.fidl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "apps/modular/mojo/strong_binding.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/logging.h"

namespace modular {

// A memory only implementation of storage for session data. We use
// until Ledger doesn't crash anymore;
// https://fuchsia.atlassian.net/browse/LE-46.
class SessionStorageImpl : public SessionStorage {
 public:
  using Storage = std::unordered_map<std::string, SessionDataPtr>;

  SessionStorageImpl(std::shared_ptr<Storage> storage,
                     const fidl::String& key,
                     fidl::InterfaceRequest<SessionStorage> request)
      : binding_(this, std::move(request)), key_(key), storage_(storage) {
    FTL_LOG(INFO) << "SessionStorageImpl() " << key_;
  }

  ~SessionStorageImpl() override {
    FTL_LOG(INFO) << "~SessionStorageImpl() " << key_;
  }

 private:
  void ReadSessionData(const ReadSessionDataCallback& cb) override {
    FTL_LOG(INFO) << "SessionStorageImpl::ReadSessionData() " << key_;

    // session_page_->GetSnapshot(GetProxy(&session_page_snapshot_),
    //                            [](ledger::Status status) {});
    // session_page_snapshot_->Get(
    //     to_array("session_data"),
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

  void WriteSessionData(SessionDataPtr data) override {
    FTL_LOG(INFO) << "SessionStorageImpl::WriteSessionData() " << key_;
    storage_->emplace(std::make_pair(key_, std::move(data)));
  }

  StrongBinding<SessionStorage> binding_;
  const std::string key_;
  std::shared_ptr<Storage> storage_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SessionStorageImpl);
};

}  // namespace modular

#endif
