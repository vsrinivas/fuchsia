// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_PAGE_CLOUD_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_PAGE_CLOUD_IMPL_H_

#include <memory>
#include <utility>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/vector.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "peridot/bin/cloud_provider_firestore/app/credentials_provider.h"
#include "peridot/bin/cloud_provider_firestore/firestore/firestore_service.h"
#include "peridot/bin/cloud_provider_firestore/firestore/listen_call_client.h"
#include "peridot/bin/cloud_provider_firestore/include/types.h"

namespace cloud_provider_firestore {

class PageCloudImpl : public cloud_provider::PageCloud,
                      public ListenCallClient {
 public:
  explicit PageCloudImpl(
      std::string page_path, CredentialsProvider* credentials_provider,
      FirestoreService* firestore_service,
      fidl::InterfaceRequest<cloud_provider::PageCloud> request);
  ~PageCloudImpl() override;

  void set_on_empty(fit::closure on_empty) { on_empty_ = std::move(on_empty); }

 private:
  void ScopedGetCredentials(
      fit::function<void(std::shared_ptr<grpc::CallCredentials>)> callback);

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

  // ListenCallClient:
  void OnConnected() override;

  void OnResponse(google::firestore::v1beta1::ListenResponse response) override;

  void OnFinished(grpc::Status status) override;

  // Handles new commits delivered from the cloud watcher.
  //
  // This will either send over the commits immediately or queue them if we're
  // waiting the the watcher to ack the previous call.
  void HandleCommits(fidl::VectorPtr<cloud_provider::Commit> commits,
                     cloud_provider::Token token);

  void SendWaitingCommits();

  // Brings down the cloud watcher.
  //
  // This can be called either because of an error, or because the client is
  // gone.
  void ShutDownWatcher();

  const std::string page_path_;
  CredentialsProvider* const credentials_provider_;
  FirestoreService* const firestore_service_;

  fidl::Binding<cloud_provider::PageCloud> binding_;
  fit::closure on_empty_;

  // Watcher set by the client.
  cloud_provider::PageCloudWatcherPtr watcher_;
  std::unique_ptr<google::protobuf::Timestamp> watcher_timestamp_or_null_;
  SetWatcherCallback set_watcher_callback_;
  std::unique_ptr<ListenCallHandler> listen_call_handler_;

  // We will only call OnNewCommits() on the watcher when the callback of the
  // previous OnNewCommits() call is already called. Any commits delivered
  // between an OnNewCommits() call and its callback executing are queued in
  // |commits_waiting_for_ack_|.
  bool waiting_for_watcher_to_ack_commits_ = false;
  fidl::VectorPtr<cloud_provider::Commit> commits_waiting_for_ack_;
  cloud_provider::Token token_for_waiting_commits_;

  // Must be the last member.
  fxl::WeakPtrFactory<PageCloudImpl> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageCloudImpl);
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_PAGE_CLOUD_IMPL_H_
