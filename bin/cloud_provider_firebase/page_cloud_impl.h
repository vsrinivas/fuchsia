// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_CLOUD_PROVIDER_FIREBASE_PAGE_CLOUD_IMPL_H_
#define APPS_LEDGER_CLOUD_PROVIDER_FIREBASE_PAGE_CLOUD_IMPL_H_

#include <memory>
#include <utility>

#include "apps/ledger/services/cloud_provider/cloud_provider.fidl.h"
#include "apps/ledger/src/auth_provider/auth_provider.h"
#include "apps/ledger/src/callback/cancellable.h"
#include "apps/ledger/src/cloud_provider/public/commit_watcher.h"
#include "apps/ledger/src/cloud_provider/public/page_cloud_handler.h"
#include "apps/ledger/src/firebase/firebase.h"
#include "apps/ledger/src/gcs/cloud_storage.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"

namespace cloud_provider_firebase {

class PageCloudImpl : public cloud_provider::PageCloud, CommitWatcher {
 public:
  PageCloudImpl(auth_provider::AuthProvider* auth_provider,
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
  void AddCommits(fidl::Array<cloud_provider::CommitPtr> commits,
                  const AddCommitsCallback& callback) override;
  void GetCommits(fidl::Array<uint8_t> min_position_token,
                  const GetCommitsCallback& callback) override;
  void AddObject(fidl::Array<uint8_t> id,
                 zx::vmo data,
                 const AddObjectCallback& callback) override;
  void GetObject(fidl::Array<uint8_t> id,
                 const GetObjectCallback& callback) override;
  void SetWatcher(
      fidl::Array<uint8_t> min_position_token,
      fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
      const SetWatcherCallback& callback) override;

  void Unregister();

  auth_provider::AuthProvider* const auth_provider_;
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

#endif  // APPS_LEDGER_CLOUD_PROVIDER_FIREBASE_PAGE_CLOUD_IMPL_H_
