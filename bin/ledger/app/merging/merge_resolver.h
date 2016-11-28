// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_MERGING_CONFLICT_RESOLVER_H_
#define APPS_LEDGER_SRC_APP_MERGING_CONFLICT_RESOLVER_H_

#include "apps/ledger/src/app/merging/merge_strategy.h"
#include "apps/ledger/src/callback/cancellable.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace ledger {
// MergeResolver watches a page and resolves conflicts as they appear using the
// provided merge strategy.
class MergeResolver : public storage::CommitWatcher {
 public:
  MergeResolver(storage::PageStorage* storage,
                std::unique_ptr<MergeStrategy> strategy);
  ~MergeResolver();

  void set_on_empty(ftl::Closure on_empty_callback);

  // Returns true if no merge is currently in progress.
  bool IsEmpty();

 private:
  // storage::CommitWatcher:
  void OnNewCommits(
      const std::vector<std::unique_ptr<const storage::Commit>>& commits,
      storage::ChangeSource source) override;

  void PostCheckConflicts();
  void CheckConflicts();
  void ResolveConflicts(std::vector<storage::CommitId> heads);
  std::unique_ptr<const storage::Commit> FindCommonAncestor(
      const std::unique_ptr<const storage::Commit>& head1,
      const std::unique_ptr<const storage::Commit>& head2);

  storage::PageStorage* const storage_;
  std::unique_ptr<MergeStrategy> const strategy_;
  callback::CancellableContainer merges_;

  // WeakPtrFactory must be the last field of the class.
  ftl::WeakPtrFactory<MergeResolver> weak_ptr_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MergeResolver);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_MERGING_CONFLICT_RESOLVER_H_
