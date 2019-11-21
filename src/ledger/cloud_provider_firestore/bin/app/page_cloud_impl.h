// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_PAGE_CLOUD_IMPL_H_
#define SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_PAGE_CLOUD_IMPL_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/vector.h>
#include <lib/fit/function.h>

#include <memory>
#include <utility>

#include "peridot/lib/rng/random.h"
#include "src/ledger/cloud_provider_firestore/bin/app/credentials_provider.h"
#include "src/ledger/cloud_provider_firestore/bin/firestore/firestore_service.h"
#include "src/ledger/cloud_provider_firestore/bin/firestore/listen_call_client.h"
#include "src/ledger/cloud_provider_firestore/bin/include/types.h"
#include "src/ledger/lib/commit_pack/commit_pack.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace cloud_provider_firestore {

class PageCloudImpl : public cloud_provider::PageCloud, public ListenCallClient {
 public:
  explicit PageCloudImpl(std::string page_path, rng::Random* random,
                         CredentialsProvider* credentials_provider,
                         FirestoreService* firestore_service,
                         fidl::InterfaceRequest<cloud_provider::PageCloud> request);
  ~PageCloudImpl() override;

  void SetOnDiscardable(fit::closure on_discardable);

  bool IsDiscardable() const;

 private:
  void ScopedGetCredentials(fit::function<void(std::shared_ptr<grpc::CallCredentials>)> callback);

  // cloud_provider::PageCloud:
  void AddCommits(cloud_provider::CommitPack commits, AddCommitsCallback callback) override;
  void GetCommits(std::unique_ptr<cloud_provider::PositionToken> min_position_token,
                  GetCommitsCallback callback) override;
  void AddObject(std::vector<uint8_t> id, fuchsia::mem::Buffer data,
                 cloud_provider::ReferencePack references, AddObjectCallback callback) override;
  void GetObject(std::vector<uint8_t> id, GetObjectCallback callback) override;
  void SetWatcher(std::unique_ptr<cloud_provider::PositionToken> min_position_token,
                  fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
                  SetWatcherCallback callback) override;
  void GetDiff(std::vector<uint8_t> commit_id, std::vector<std::vector<uint8_t>> possible_bases,
               GetDiffCallback callback) override;
  void UpdateClock(cloud_provider::ClockPack, UpdateClockCallback callback) override;

  // ListenCallClient:
  void OnConnected() override;

  void OnResponse(google::firestore::v1beta1::ListenResponse response) override;

  void OnFinished(grpc::Status status) override;

  // Handles new commits delivered from the cloud watcher.
  //
  // This will either send over the commits immediately or queue them if we're
  // waiting the the watcher to ack the previous call.
  void HandleCommits(std::vector<cloud_provider::CommitPackEntry> commit_entries,
                     cloud_provider::PositionToken token);

  void SendWaitingCommits();

  // Brings down the cloud watcher.
  //
  // This can be called either because of an error, or because the client is
  // gone.
  void ShutDownWatcher();

  const std::string page_path_;
  rng::Random* const random_;
  CredentialsProvider* const credentials_provider_;
  FirestoreService* const firestore_service_;

  fidl::Binding<cloud_provider::PageCloud> binding_;
  fit::closure on_discardable_;

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
  std::vector<cloud_provider::CommitPackEntry> commits_waiting_for_ack_;
  cloud_provider::PositionToken token_for_waiting_commits_;

  // Must be the last member.
  fxl::WeakPtrFactory<PageCloudImpl> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageCloudImpl);
};

}  // namespace cloud_provider_firestore

#endif  // SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_PAGE_CLOUD_IMPL_H_
