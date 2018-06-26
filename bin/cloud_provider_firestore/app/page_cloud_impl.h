// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_PAGE_CLOUD_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_PAGE_CLOUD_IMPL_H_

#include <memory>
#include <utility>

#include <fuchsia/ledger/cloud/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/vector.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/cloud_provider_firestore/app/credentials_provider.h"
#include "peridot/bin/cloud_provider_firestore/firestore/firestore_service.h"
#include "peridot/bin/cloud_provider_firestore/include/types.h"

namespace cloud_provider_firestore {

class PageCloudImpl : public cloud_provider::PageCloud {
 public:
  explicit PageCloudImpl(
      std::string page_path, CredentialsProvider* credentials_provider,
      FirestoreService* firestore_service,
      fidl::InterfaceRequest<cloud_provider::PageCloud> request);
  ~PageCloudImpl() override;

  void set_on_empty(const fxl::Closure& on_empty) { on_empty_ = on_empty; }

 private:
  void ScopedGetCredentials(
      std::function<void(std::shared_ptr<grpc::CallCredentials>)> callback);

  // cloud_provider::PageCloud:
  void AddCommits(fidl::VectorPtr<cloud_provider::Commit> commits,
                  AddCommitsCallback callback) override;
  void GetCommits(std::unique_ptr<cloud_provider::Token> min_position_token,
                  GetCommitsCallback callback) override;
  void AddObject(fidl::VectorPtr<uint8_t> id, fuchsia::mem::Buffer data,
                 AddObjectCallback callback) override;
  void GetObject(fidl::VectorPtr<uint8_t> id,
                 GetObjectCallback callback) override;
  void SetWatcher(
      std::unique_ptr<cloud_provider::Token> min_position_token,
      fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
      SetWatcherCallback callback) override;

  const std::string page_path_;
  CredentialsProvider* const credentials_provider_;
  FirestoreService* const firestore_service_;

  fidl::Binding<cloud_provider::PageCloud> binding_;
  fxl::Closure on_empty_;

  // Must be the last member.
  fxl::WeakPtrFactory<PageCloudImpl> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageCloudImpl);
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_PAGE_CLOUD_IMPL_H_
