// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_MERGING_CONFLICT_RESOLVER_CLIENT_H_
#define APPS_LEDGER_SRC_APP_MERGING_CONFLICT_RESOLVER_CLIENT_H_

#include <memory>
#include <vector>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/app/page_manager.h"
#include "apps/ledger/src/callback/operation_serializer.h"
#include "apps/ledger/src/callback/waiter.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace ledger {
// Client handling communication with a ConflictResolver interface in order to
// merge conflicting commit branches. It is used both by AutoMergeStrategy and
// CustomMergeStrategy.
class ConflictResolverClient : public MergeResultProvider {
 public:
  explicit ConflictResolverClient(
      storage::PageStorage* storage,
      PageManager* page_manager,
      ConflictResolver* conflict_resolver,
      std::unique_ptr<const storage::Commit> left,
      std::unique_ptr<const storage::Commit> right,
      std::unique_ptr<const storage::Commit> ancestor,
      ftl::Closure on_done);
  ~ConflictResolverClient();

  void Start();
  void Cancel();

 private:
  void OnNextMergeResult(
      const MergedValuePtr& merged_value,
      const ftl::RefPtr<callback::Waiter<storage::Status, storage::ObjectId>>&
          waiter);
  void Finalize();

  // MergeResultProvider:
  void GetDiff(fidl::Array<uint8_t> token, const GetDiffCallback& callback);
  void Merge(fidl::Array<MergedValuePtr> merge_changes,
             const MergeCallback& callback);
  void Done(const DoneCallback& callback);

  storage::PageStorage* const storage_;
  PageManager* const manager_;
  ConflictResolver* const conflict_resolver_;

  std::unique_ptr<const storage::Commit> const left_;
  std::unique_ptr<const storage::Commit> const right_;
  std::unique_ptr<const storage::Commit> const ancestor_;

  ftl::Closure on_done_;

  std::unique_ptr<storage::Journal> journal_;
  // |in_client_request_| is true when waiting for the callback of the
  // ConflictResolver.Resolve call. When this merge is cancelled, we check this
  // boolean to know if we should abort immediately (when in a client request,
  // as the client may have disconnected) and when we should wait for the
  // operation to finish (the other cases, such as committing the merge).
  bool in_client_request_ = false;
  bool cancelled_ = false;
  callback::OperationSerializer<Status> operation_serializer_;

  fidl::Binding<MergeResultProvider> merge_result_provider_binding_;

  // This must be the last member of the class.
  ftl::WeakPtrFactory<ConflictResolverClient> weak_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ConflictResolverClient);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_MERGING_CONFLICT_RESOLVER_CLIENT_H_
