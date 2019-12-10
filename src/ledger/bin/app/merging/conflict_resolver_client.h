// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_MERGING_CONFLICT_RESOLVER_CLIENT_H_
#define SRC_LEDGER_BIN_APP_MERGING_CONFLICT_RESOLVER_CLIENT_H_

#include <lib/fit/function.h>

#include <memory>
#include <vector>

#include "src/ledger/bin/app/active_page_manager.h"
#include "src/ledger/bin/app/diff_utils.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/fidl/syncable.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/lib/callback/operation_serializer.h"
#include "src/lib/callback/waiter.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace ledger {
// Client handling communication with a ConflictResolver interface in order to
// merge conflicting commit branches. It is used both by AutoMergeStrategy and
// CustomMergeStrategy.
class ConflictResolverClient : public fuchsia::ledger::MergeResultProviderSyncableDelegate {
 public:
  explicit ConflictResolverClient(storage::PageStorage* storage,
                                  ActivePageManager* active_page_manager,
                                  ConflictResolver* conflict_resolver,
                                  std::unique_ptr<const storage::Commit> left,
                                  std::unique_ptr<const storage::Commit> right,
                                  std::unique_ptr<const storage::Commit> ancestor,
                                  fit::function<void(Status)> callback);
  ConflictResolverClient(const ConflictResolverClient&) = delete;
  ConflictResolverClient& operator=(const ConflictResolverClient&) = delete;
  ~ConflictResolverClient() override;

  void Start();
  void Cancel();

 private:
  // Gets or creates the object identifier associated to the given
  // |merge_value|. This method can only be called on merge values whose source
  // is either |NEW| or |RIGHT|.
  void GetOrCreateObjectIdentifier(const MergedValue& merged_value,
                                   fit::function<void(Status, storage::ObjectIdentifier)> callback);

  // Rolls back journal, closes merge result provider and invokes callback_ with
  // |status|. This method must be called at most once.
  void Finalize(Status status);

  // Performs a diff of the given type on the conflict.
  void GetDiff(
      diff_utils::DiffType type, std::unique_ptr<Token> token,
      fit::function<void(Status, std::vector<DiffEntry>, std::unique_ptr<Token>)> callback);

  // MergeResultProviderNotifierDelegate:
  void GetFullDiff(std::unique_ptr<Token> token,
                   fit::function<void(Status, std::vector<DiffEntry>, std::unique_ptr<Token>)>
                       callback) override;
  void GetConflictingDiff(
      std::unique_ptr<Token> token,
      fit::function<void(Status, std::vector<DiffEntry>, std::unique_ptr<Token>)> callback)
      override;
  void Merge(std::vector<MergedValue> merged_values, fit::function<void(Status)> callback) override;
  void MergeNonConflictingEntries(fit::function<void(Status)> callback) override;
  void Done(fit::function<void(Status)> callback) override;

  // Checks whether this ConflictResolverClient is still valid (not deleted nor
  // cancelled) and the status is OK. Returns |true| in that case. Otherwise,
  // calls |callback| with the given |status| and calls |Finalize| if this
  // object is not deleted, then return |false|.
  static bool IsInValidStateAndNotify(const fxl::WeakPtr<ConflictResolverClient>& weak_this,
                                      const fit::function<void(Status)>& callback,
                                      Status status = Status::OK);

  storage::PageStorage* const storage_;
  ActivePageManager* const manager_;
  ConflictResolver* const conflict_resolver_;

  std::unique_ptr<const storage::Commit> const left_;
  std::unique_ptr<const storage::Commit> const right_;
  std::unique_ptr<const storage::Commit> const ancestor_;

  // Called when the merge process is finished.
  fit::function<void(Status)> callback_;

  // |has_merged_values_| is true when |Merge| has been called to set some
  // values. It is used as an optimization in |MergeNonConflictingEntries|.
  bool has_merged_values_ = false;
  std::unique_ptr<storage::Journal> journal_;
  // |in_client_request_| is true when waiting for the callback of the
  // ConflictResolver.Resolve call. When this merge is cancelled, we check this
  // boolean to know if we should abort immediately (when in a client request,
  // as the client may have disconnected) and when we should wait for the
  // operation to finish (the other cases, such as committing the merge).
  bool in_client_request_ = false;
  bool cancelled_ = false;

  // Operations are operating on the state of the merge commit. They must be
  // serialized.
  OperationSerializer operation_serializer_;

  SyncableBinding<fuchsia::ledger::MergeResultProviderSyncableDelegate>
      merge_result_provider_binding_;

  // This must be the last member of the class.
  fxl::WeakPtrFactory<ConflictResolverClient> weak_factory_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_MERGING_CONFLICT_RESOLVER_CLIENT_H_
