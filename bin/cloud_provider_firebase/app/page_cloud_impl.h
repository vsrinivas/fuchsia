// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_APP_PAGE_CLOUD_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_APP_PAGE_CLOUD_IMPL_H_

#include <memory>
#include <utility>

#include <fuchsia/ledger/cloud/cpp/fidl.h>

#include "lib/callback/cancellable.h"
#include "lib/fidl/cpp/array.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/cloud_provider_firebase/gcs/cloud_storage.h"
#include "peridot/bin/cloud_provider_firebase/include/types.h"
#include "peridot/bin/cloud_provider_firebase/page_handler/public/commit_watcher.h"
#include "peridot/bin/cloud_provider_firebase/page_handler/public/page_cloud_handler.h"
#include "peridot/lib/firebase/firebase.h"
#include "peridot/lib/firebase_auth/firebase_auth.h"

namespace cloud_provider_firebase {

class PageCloudImpl : public cloud_provider::PageCloud, CommitWatcher {
 public:
  PageCloudImpl(firebase_auth::FirebaseAuth* firebase_auth,
                std::unique_ptr<firebase::Firebase> firebase,
                std::unique_ptr<gcs::CloudStorage> cloud_storage,
                std::unique_ptr<PageCloudHandler> handler,
                fidl::InterfaceRequest<cloud_provider::PageCloud> request);
  ~PageCloudImpl() override;

  void set_on_empty(const fxl::Closure& on_empty) { on_empty_ = on_empty; }

  // CommitWatcher:
  void OnRemoteCommits(std::vector<Record> records) override;

  void OnConnectionError() override;

  void OnTokenExpired() override;

  void OnMalformedNotification() override;

 private:
  void SendRemoteCommits();

  // cloud_provider::PageCloud:
  void AddCommits(fidl::VectorPtr<cloud_provider::Commit> commits,
                  AddCommitsCallback callback) override;
  void GetCommits(std::unique_ptr<cloud_provider::Token> min_position_token,
                  GetCommitsCallback callback) override;
  void AddObject(fidl::VectorPtr<uint8_t> id, fuchsia::mem::Buffer data,
                 AddObjectCallback callback) override;
  void GetObject(fidl::VectorPtr<uint8_t> id, GetObjectCallback) override;
  void SetWatcher(
      std::unique_ptr<cloud_provider::Token> min_position_token,
      fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
      SetWatcherCallback callback) override;

  void Unregister();

  firebase_auth::FirebaseAuth* const firebase_auth_;
  std::unique_ptr<firebase::Firebase> firebase_;
  std::unique_ptr<gcs::CloudStorage> cloud_storage_;
  std::unique_ptr<PageCloudHandler> handler_;
  fidl::Binding<cloud_provider::PageCloud> binding_;
  fxl::Closure on_empty_;

  // Remote commits accumulated until the client confirms receiving the previous
  // notifiation.
  std::vector<Record> records_;
  bool waiting_for_remote_commits_ack_ = false;

  // Watcher set by the client.
  cloud_provider::PageCloudWatcherPtr watcher_;
  // Whether this class is registered as commit watcher in |handler_|.
  bool handler_watcher_set_ = false;

  // Pending auth token requests to be cancelled when this class goes away.
  callback::CancellableContainer auth_token_requests_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageCloudImpl);
};

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_APP_PAGE_CLOUD_IMPL_H_
